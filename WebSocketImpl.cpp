#include "WebSocketImpl.h"

#include "Looper.h"

#include <iostream>
#include <memory>
#include <algorithm>
#include <libwebsockets.h>

#define WS_RX_BUFFER_SIZE ((1 << 16) - 1)
#define WS_REVERSED_RECEIVE_BUFFER_SIZE  (1 << 12)


////////////////////net thread - begin ///////////////////

//////////////basic data type - begin /////////////
enum class NetCmdType
{
    OPEN, CLOSE, WRITE, RECIEVE
};

class NetDataPack {
public:
    NetDataPack() {}
    NetDataPack(const char *f, size_t l, bool isBinary) {
        _data = (uint8_t*)calloc(1, l + LWS_PRE);
        memcpy(_data + LWS_PRE, f, l);
        _size = l;
        _remain = l;
        _payload = _data + LWS_PRE;
        _isBinary = isBinary;
    }
    ~NetDataPack() {
        if (_data) {
            free(_data);
            _data = nullptr;
        }
        _size = 0;
    }

    NetDataPack(const NetDataPack &) = delete;
    NetDataPack(NetDataPack&&) = delete;

    size_t remain() { return _remain; }
    uint8_t *payload() { return _payload; }
    
    void consume(size_t d) 
    {
        assert(d <= _remain);
        _payload += d;
        _remain -= d;
        _consumed += d;
    }

    size_t consumed() { return _consumed; }
    bool isBinary() { return _isBinary; }
private:
    uint8_t *_data = nullptr;
    uint8_t *_payload = nullptr;
    size_t _size = 0;
    size_t _remain = 0;
    bool _isBinary = true;
    size_t _consumed = 0;
};

class NetCmd {
public:
    NetCmd() {}
    NetCmd(WebSocketImpl *ws, NetCmdType cmd, std::shared_ptr<NetDataPack> data) :ws(ws), cmd(cmd), data(data) {}
    NetCmd(const NetCmd &o) :ws(o.ws), cmd(o.cmd), data(o.data) {}
    static NetCmd Open(WebSocketImpl *ws);
    static NetCmd Close(WebSocketImpl *ws);
    static NetCmd Write(WebSocketImpl *ws, const char *data, size_t len, bool isBinary);
public:
    WebSocketImpl *ws{nullptr};
    NetCmdType cmd;
    std::shared_ptr<NetDataPack> data;
};

NetCmd NetCmd::Open(WebSocketImpl *ws) { return NetCmd(ws, NetCmdType::OPEN, nullptr); }
NetCmd NetCmd::Close(WebSocketImpl *ws) { return NetCmd(ws, NetCmdType::CLOSE, nullptr); }
NetCmd NetCmd::Write(WebSocketImpl *ws, const char *data, size_t len, bool isBinary)
{ 
    auto pack= std::make_shared<NetDataPack>(data, len, isBinary);
    return NetCmd(ws, NetCmdType::WRITE, pack); 
}

//////////////basic data type - end /////////////

static int websocket_callback(lws *wsi, enum lws_callback_reasons reason, void *user, void *in, ssize_t len)
{
    if (wsi == nullptr) return 0;
    int ret = 0;
    WebSocketImpl *ws = (WebSocketImpl*)lws_wsi_user(wsi);
    if (ws) {
        ws->lwsCallback(wsi, reason, user, in, len);
    }
    return ret;
}

/////////////loop thread - begin /////////////////


class Helper :public Loop, public std::enable_shared_from_this<Helper>
{
public:

    Helper();
    virtual ~Helper();

    static std::shared_ptr<Helper> getInstance();
    
    void init();

    void send(const std::string &event, const NetCmd &cmd);

    void before() override;
    void after() override;
    void update(int dtms) override;

    void runInUI(const std::function<void()> &fn);

    void handleCmdConnect(NetCmd &cmd);
    void handleCmdDisconnect(NetCmd &cmd);
    void handleCmdWrite(NetCmd &cmd);

    uv_loop_t * getUVLoop() { return _netThread->getUVLoop(); }
    void updateLibUV();
private:
    //libwebsocket helper
    void initProtocols();
    lws_context_creation_info initCtxCreateInfo(const struct lws_protocols *protocols, bool useSSL);

private:
    static std::shared_ptr<Helper> __sInstance;
    static std::mutex __sInstanceMutex;

    std::shared_ptr<Looper<NetCmd> > _netThread = nullptr;
    
public:
    //libwebsocket fields
    lws_protocols * _lwsDefaultProtocols = nullptr;
    lws_context *_lwsContext = nullptr;

    //friend class WebSocketImpl;
};

//static fields
std::shared_ptr<Helper> Helper::__sInstance;
std::mutex Helper::__sInstanceMutex;

Helper::Helper()
{}

Helper::~Helper()
{

    if (_netThread) {
        _netThread->syncStop(); //use async?
        _netThread.reset();
    }
    if (_lwsContext)
    {
        lws_libuv_stop(_lwsContext);
        lws_context_destroy(_lwsContext);
        _lwsContext = nullptr;
    }
    if (_lwsDefaultProtocols)
    {
        free(_lwsDefaultProtocols);
        _lwsDefaultProtocols = nullptr;
    }


}

std::shared_ptr<Helper> Helper::getInstance()
{
    std::lock_guard<std::mutex> guard(__sInstanceMutex);
    if (!__sInstance) {
        __sInstance = std::make_shared<Helper>();
        __sInstance->init();
    }
    return __sInstance;
}

void Helper::init()
{
    _netThread = std::make_shared<Looper<NetCmd> >(ThreadCategory::NET_THREAD, shared_from_this(), 5000);

    initProtocols();
    lws_context_creation_info  info = initCtxCreateInfo(_lwsDefaultProtocols, false);
    _lwsContext = lws_create_context(&info);

    _netThread->on("open", [this](NetCmd &ev) {this->handleCmdConnect(ev); });
    _netThread->on("send", [this](NetCmd &ev) {this->handleCmdWrite(ev); });
    _netThread->on("close", [this](NetCmd& ev) {this->handleCmdDisconnect(ev); });

    _netThread->run();
}

void Helper::initProtocols()
{
    if (!_lwsDefaultProtocols) free(_lwsDefaultProtocols); 
    _lwsDefaultProtocols = (lws_protocols *)calloc(2, sizeof(struct lws_protocols));
    lws_protocols *p = &_lwsDefaultProtocols[0];
    p->name = "";
    p->rx_buffer_size = WS_RX_BUFFER_SIZE;
    p->callback = (lws_callback_function*) &websocket_callback;
    p->id = (1ULL << 32) - 1ULL;
}

lws_context_creation_info Helper::initCtxCreateInfo(const struct lws_protocols *protocols, bool useSSL)
{
    lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    
    info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS |
        LWS_SERVER_OPTION_LIBUV |
        LWS_SERVER_OPTION_PEER_CERT_NOT_REQUIRED;

    if (useSSL)
    {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    }
    info.user = nullptr;
    return info;
}

void Helper::send(const std::string &event, const NetCmd &cmd)
{
    NetCmd _copy(cmd);
    _netThread->emit(event, _copy);
}

void Helper::before()
{
    std::cout << "[Helper] thread start ... " << std::endl;
    updateLibUV();
}

void Helper::update(int dtms)
{
    std::cout << "[Helper] thread tick ... " << std::endl;
}

void Helper::after()
{
    std::cout << "[Helper] thread quit!!! ... " << std::endl;
}

void Helper::runInUI(const std::function<void()> &fn)
{
    //TODO dispatch to main thread
    fn();
}

void Helper::handleCmdConnect(NetCmd &cmd)
{
    cmd.ws->doConnect();
}

void Helper::handleCmdDisconnect(NetCmd &cmd)
{
    cmd.ws->doDisconnect();
    lws_callback_on_writable(cmd.ws->_wsi);
}

void Helper::handleCmdWrite(NetCmd &cmd)
{
    auto pack = cmd.data;
    cmd.ws->_sendBuffer.push_back(pack);
    lws_callback_on_writable(cmd.ws->_wsi);
}

void Helper::updateLibUV()
{
    lws_uv_initloop(Helper::getInstance()->_lwsContext, Helper::getInstance()->getUVLoop(), 0);
}


/////////////loop thread - end //////////////////


////////////////////net thread - end   ///////////////////

int WebSocketImpl::_protocolCounter = 1;
std::atomic_int64_t WebSocketImpl::_wsIdCounter = 1;
std::unordered_map<int64_t, WebSocketImpl::Ptr > WebSocketImpl::_cachedSocketes;

///////friend function 
static WebSocketImpl::Ptr findWs(int64_t wsId)
{
    auto it = WebSocketImpl::_cachedSocketes.find(wsId);
    return it == WebSocketImpl::_cachedSocketes.end() ? nullptr : it->second;
}

WebSocketImpl::WebSocketImpl(WebSocket *t)
{
    _ws = t;
    _wsId = _wsIdCounter.fetch_add(1);
}

WebSocketImpl::~WebSocketImpl()
{
    _cachedSocketes.erase(_wsId);

    if (_lwsProtocols) {
        free(_lwsProtocols);
        _lwsProtocols = nullptr;
    }
    if (_lwsHost) {
        //TODO destroy function not found!
        _lwsHost = nullptr;
    }
    if (_wsi) {
        //TODO destroy lws
        _wsi = nullptr;
    }
}

bool WebSocketImpl::init(const std::string &uri, WebSocketDelegate::Ptr delegate, const std::vector<std::string> &protocols, const std::string & caFile)
{
    _cachedSocketes.emplace(_wsId, shared_from_this());

    this->_uri = uri;
    this->_delegate = delegate;
    this->_protocols = protocols;
    this->_caFile = caFile;

    if (this->_uri.size())
        return false;

    size_t size = protocols.size();
    if (size > 0) 
    {
        _lwsProtocols = (struct lws_protocols*)calloc(size + 1, sizeof(struct lws_protocols));
        for (int i = 0; i < size; i++) 
        {
            struct lws_protocols *p = &_lwsProtocols[i];
            p->name = this->_protocols[i].data();
            p->id = (++_protocolCounter);
            p->rx_buffer_size = WS_RX_BUFFER_SIZE;
            p->per_session_data_size = 0;
            p->user = this;
            p->callback = (lws_callback_function*)&websocket_callback;
            _joinedProtocols += protocols[i];
            if (i < size - 1) _joinedProtocols += ",";
        }
    }

    Helper::getInstance()->send("open", NetCmd::Open(this));

    return true;
}

void WebSocketImpl::sigClose()
{
    Helper::getInstance()->send("close", NetCmd::Close(this));
}

void WebSocketImpl::sigCloseAsync()
{
    Helper::getInstance()->send("close", NetCmd::Close(this));
    //sleep forever
    while (_state != WebSocket::State::CLOSED)
    {
        std::this_thread::yield();
    }
}

void WebSocketImpl::sigSend(const char *data, size_t len)
{
    Helper::getInstance()->send("send", NetCmd::Write(this, data, len, true));
}

void WebSocketImpl::sigSend(const std::string &msg)
{
    NetCmd cmd = NetCmd::Write(this, msg.data(), msg.length(), false);
    Helper::getInstance()->send("send", cmd);
}

int WebSocketImpl::lwsCallback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, ssize_t len)
{
    int ret = 0;
    switch (reason)
    {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        ret = netOnConnected();
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        ret = netOnError(WebSocket::ErrorCode::CONNECTION_FAIURE);
        break;
    case LWS_CALLBACK_CLIENT_RECEIVE:
        ret = netOnReadable(in, (size_t)len);
        break;
    case LWS_CALLBACK_CLIENT_WRITEABLE:
        ret = netOnWritable();
        break;
    case LWS_CALLBACK_WSI_DESTROY:
        ret = netOnClosed();
        break;
    case LWS_CALLBACK_PROTOCOL_INIT:
    case LWS_CALLBACK_PROTOCOL_DESTROY:
    case LWS_CALLBACK_WSI_CREATE:
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLOSED:
    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION:
    case LWS_CALLBACK_RAW_CLOSE:
    case LWS_CALLBACK_RAW_WRITEABLE:
    default:
        lwsl_warn("lws callback reason %d is not handled!\n", reason);
        break;
    }
    return ret;
}


void WebSocketImpl::doConnect()
{
    
    assert(Helper::getInstance()->getUVLoop());

    struct lws_extension exts[] = {
        {
            "permessage-deflate",
            lws_extension_callback_pm_deflate,
            "permessage-deflate; client_max_window_bits"
        },
        {
            "deflate-frame",
            lws_extension_callback_pm_deflate,
            "deflate-frame"
        },
    { nullptr,nullptr,nullptr }
    };

    auto useSSL = false; //TODO calculate from url

    lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = _lwsProtocols == nullptr ? Helper::getInstance()->_lwsDefaultProtocols : _lwsProtocols;
    info.gid = -1;
    info.uid = -1;
    info.user = this;
    info.ssl_ca_filepath = _caFile.empty() ? nullptr : _caFile.c_str();

    info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS |
        LWS_SERVER_OPTION_LIBUV;
    //ssl flags
    int sslFlags = 0;

    if (useSSL)
    {
        info.options = info.options | LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT |
            LWS_SERVER_OPTION_PEER_CERT_NOT_REQUIRED;
        sslFlags = sslFlags | LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED |
            LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LCCSCF_ALLOW_EXPIRED;
    }

    _lwsHost = lws_create_vhost(Helper::getInstance()->_lwsContext, &info);

    if (useSSL)
    {
        lws_init_vhost_client_ssl(&info, _lwsHost); //
    }


    struct lws_client_connect_info cinfo;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.context = Helper::getInstance()->_lwsContext;
    cinfo.address = "invoke.top";
    cinfo.port = 6789;
    cinfo.ssl_connection = sslFlags;
    cinfo.path = "/";
    cinfo.host = "invoke.top";
    cinfo.origin = "invoke.top";
    cinfo.protocol = _joinedProtocols.empty() ? "" : _joinedProtocols.c_str();
    cinfo.ietf_version_or_minus_one = -1;
    cinfo.userdata = this;
    cinfo.client_exts = exts;
    cinfo.vhost = _lwsHost;

    _wsi = lws_client_connect_via_info(&cinfo);

    if (_wsi == nullptr)
        netOnError(WebSocket::ErrorCode::LWS_ERROR);

    Helper::getInstance()->updateLibUV();
}

void WebSocketImpl::doDisconnect()
{
    if (_state == WebSocket::State::CLOSED) return;
    _state = WebSocket::State::CLOSING;
}

void WebSocketImpl::doWrite(NetDataPack &pack)
{
    const size_t bufferSize = WS_RX_BUFFER_SIZE;
    const size_t frameSize = bufferSize > pack.remain() ? pack.remain() : bufferSize; //min

    int writeProtocol = 0;
    if (pack.consumed() == 0)
        writeProtocol |= (pack.isBinary() ? LWS_WRITE_BINARY : LWS_WRITE_TEXT);
    else
        writeProtocol |= LWS_WRITE_CONTINUATION;

    if (frameSize < pack.remain())
        writeProtocol |= LWS_WRITE_NO_FIN;

    size_t bytesWrite = lws_write(_wsi, pack.payload(), frameSize, (lws_write_protocol)writeProtocol);

    if (bytesWrite < 0)
    {
        //error 
        sigCloseAsync();
    }
    else 
    {
        pack.consume(bytesWrite);
    }
}

int WebSocketImpl::netOnError(WebSocket::ErrorCode ecode)
{
    auto code = static_cast<int>(ecode);
    std::cout << "connection error: " << code << std::endl;
    Helper::getInstance()->runInUI([this, code]() {
        this->_delegate->onError(*(this->_ws), static_cast<int>(code)); //FIXME error code
    });

    //change state to CLOSED
    netOnClosed();

    return 0;
}

int WebSocketImpl::netOnConnected()
{
    std::cout << "connected!" << std::endl; 
    _state = WebSocket::State::OPEN;
    auto wsi = this->_wsi;
    Helper::getInstance()->runInUI([this, wsi]() {
        this->_delegate->onConnected(*(this->_ws)); 
        lws_callback_on_writable(wsi);
    });
    return 0;
}

int WebSocketImpl::netOnClosed()
{
    _state = WebSocket::State::CLOSED;
    auto self = shared_from_this();
    Helper::getInstance()->runInUI([self]() {
        self->_delegate->onDisconnected(*(self->_ws));
    });

    //remove from cache
    _cachedSocketes.erase(_wsId);

    if (_cachedSocketes.size() == 0)
    {
        //no active websocket, quit netThread
        Helper::getInstance().reset(); 
    }

    return 0;
}

int WebSocketImpl::netOnReadable(void *in, size_t len)
{
    std::cout << "readable : " << len << std::endl;
    if (in && len > 0) {
        _receiveBuffer.insert(_receiveBuffer.end(), (uint8_t*)in, (uint8_t*)in + len);
    }
    
    auto remainSize = lws_remaining_packet_payload(_wsi);
    auto isFinalFrag = lws_is_final_fragment(_wsi);

    if (remainSize == 0 && isFinalFrag)
    {
        auto rbuffCopy = std::make_shared<std::vector<uint8_t>>(std::move(_receiveBuffer));
        
        _receiveBuffer.reserve(WS_REVERSED_RECEIVE_BUFFER_SIZE);

        bool isBinary = (lws_frame_is_binary(_wsi) != 0);

        Helper::getInstance()->runInUI([rbuffCopy, this, isBinary]() {
            WebSocket::Data data((char*)(rbuffCopy->data()), rbuffCopy->size(), isBinary);
            this->_delegate->onMesage(*(this->_ws), data);
        });
    }
    return 0;
}

int WebSocketImpl::netOnWritable()
{
    std::cout << "writable" << std::endl;

    //handle close
    if (_state == WebSocket::State::CLOSING)
    {
        lwsl_warn("closing websocket\n");
        return -1;
    }

    //pop sent packs
    while (_sendBuffer.size() > 0 && _sendBuffer.front()->remain() == 0)
    {
        _sendBuffer.pop_front();
    }

    if (_sendBuffer.size() > 0)
    {
        auto &pack = _sendBuffer.front();
        if (pack->remain() > 0) {
            doWrite(*pack);
        }
    }

    if (_wsi && _sendBuffer.size() > 0)
        lws_callback_on_writable(_wsi);

    return 0;
}


