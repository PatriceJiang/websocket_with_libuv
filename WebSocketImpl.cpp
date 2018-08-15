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
    NetCmd(WebSocketImpl *ws, NetCmdType cmd, std::shared_ptr<NetDataPack> data) :ws(ws), cmd(cmd), data(data) {}
    NetCmd(const NetCmd &o) :ws(o.ws), cmd(o.cmd), data(o.data) {}
    static NetCmd Open(WebSocketImpl *ws);
    static NetCmd Close(WebSocketImpl *ws);
    static NetCmd Write(WebSocketImpl *ws, const char *data, int len, bool isBinary);
public:
    WebSocketImpl *ws{nullptr};
    NetCmdType cmd;
    std::shared_ptr<NetDataPack> data;
};

NetCmd NetCmd::Open(WebSocketImpl *ws) { return NetCmd(ws, NetCmdType::OPEN, nullptr); }
NetCmd NetCmd::Close(WebSocketImpl *ws) { return NetCmd(ws, NetCmdType::CLOSE, nullptr); }
NetCmd NetCmd::Write(WebSocketImpl *ws, const char *data, int len, bool isBinary)
{ 
    auto pack= std::make_shared<NetDataPack>(data, len, isBinary);
    return NetCmd(ws, NetCmdType::OPEN, pack); 
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

    void before() override;
    void after() override;
    void update(int dtms) override;

    void send(NetCmd cmd);

    void runInUI(const std::function<void()> &fn);

    void handleCmdConnect(NetCmd &cmd);
    void handleCmdDisconnect(NetCmd &cmd);
    void handleCmdWrite(NetCmd &cmd);

    uv_loop_t * getLoop() { return netThread->getUVLoop(); }
    void updateLibUV();
private:
    static std::shared_ptr<Helper> sInstance;
    static std::mutex sInstanceMutex;

    Looper<NetCmd> *netThread{nullptr};

    //libwebsocket helper
    void initProtocols();
    lws_context_creation_info initCtxCreateInfo(const struct lws_protocols *protocols, bool useSSL);


    //libwebsocket fields
    lws_context *lwsContext{nullptr};
    lws_protocols *lwsDefaultProtocols{ nullptr };

    friend class WebSocketImpl;
};

//static fields
std::shared_ptr<Helper> Helper::sInstance;
std::mutex Helper::sInstanceMutex;

Helper::Helper()
{
    netThread = new Looper<NetCmd>(ThreadCategory::NET_THREAD, shared_from_this(), 1000);
}

Helper::~Helper()
{
    if (netThread) {
        netThread->syncStop();
        netThread = nullptr;
    }
    if (lwsContext)
    {
        lws_context_destroy(lwsContext);
        lwsContext = nullptr;
    }
    if (lwsDefaultProtocols)
    {
        free(lwsDefaultProtocols);
        lwsDefaultProtocols = nullptr;
    }
}

std::shared_ptr<Helper> Helper::getInstance()
{
    std::lock_guard<std::mutex> guard(sInstanceMutex);
    if (!sInstance) {
        sInstance = std::make_shared<Helper>();
    }
    return sInstance;
}

void Helper::init()
{
    initProtocols();
    lws_context_creation_info  info = initCtxCreateInfo(lwsDefaultProtocols, true);
    lwsContext = lws_create_context(&info);

    netThread->on("connect", [this](NetCmd &ev) {this->handleCmdConnect(ev); });
    netThread->on("send", [this](NetCmd &ev) {this->handleCmdWrite(ev); });
    netThread->on("close", [this](NetCmd& ev) {this->handleCmdDisconnect(ev); });
}

void Helper::initProtocols()
{
    if (!lwsDefaultProtocols) free(lwsDefaultProtocols); 
    lwsDefaultProtocols = (lws_protocols *)calloc(2, sizeof(struct lws_protocols));
    lws_protocols *p = &lwsDefaultProtocols[0];
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


void Helper::before()
{
    
}

void Helper::update(int dtms)
{

}

void Helper::after()
{

}

void Helper::handleCmdConnect(NetCmd &cmd)
{
    cmd.ws->connect();
}

void Helper::handleCmdDisconnect(NetCmd &cmd)
{
    cmd.ws->disconnect();
}

void Helper::handleCmdWrite(NetCmd &cmd)
{
    auto pack = cmd.data;
    cmd.ws->_sendBuffer.push_back(pack);
    lws_callback_on_writable(cmd.ws->lwsObj);
}

void Helper::updateLibUV()
{
    lws_uv_initloop(Helper::getInstance()->lwsContext, Helper::getInstance()->getLoop(), 0);
}


/////////////loop thread - end //////////////////


////////////////////net thread - end   ///////////////////

int WebSocketImpl::protocolCounter = 1;
std::atomic_int64_t WebSocketImpl::wsIdCounter = 1;
std::unordered_map<int64_t, WebSocketImpl::Ptr > WebSocketImpl::cachedSocketes;

///////friend function 
static WebSocketImpl::Ptr findWs(int64_t wsId)
{
    auto it = WebSocketImpl::cachedSocketes.find(wsId);
    return it == WebSocketImpl::cachedSocketes.end() ? nullptr : it->second;
}

WebSocketImpl::WebSocketImpl(WebSocket *t)
{
    ws = t;
    wsId = wsIdCounter.fetch_add(1);
    cachedSocketes.emplace(wsId, shared_from_this());
}

WebSocketImpl::~WebSocketImpl()
{
    cachedSocketes.erase(wsId);

    if (lwsProtocols) {
        free(lwsProtocols);
        lwsProtocols = nullptr;
    }
    if (lwsHost) {
        //TODO destroy function not found!
        lwsHost = nullptr;
    }
    if (lwsObj) {
        //TODO destroy lws
        lwsObj = nullptr;
    }
}

bool WebSocketImpl::init(const std::string &uri, WebSocketDelegate::Ptr delegate, const std::vector<std::string> &protocols, const std::string & caFile)
{
    this->uri = uri;
    this->_delegate = delegate;
    this->protocols = protocols;
    this->caFile = caFile;

    if (this->uri.size())
        return false;

    size_t size = protocols.size();
    if (size > 0) 
    {
        lwsProtocols = (struct lws_protocols*)calloc(size + 1, sizeof(struct lws_protocols));
        for (int i = 0; i < size; i++) 
        {
            struct lws_protocols *p = &lwsProtocols[i];
            p->name = this->protocols[i].data();
            p->id = (++protocolCounter);
            p->rx_buffer_size = WS_RX_BUFFER_SIZE;
            p->per_session_data_size = 0;
            p->user = this;
            p->callback = (lws_callback_function*)&websocket_callback;
            joinedProtocols += protocols[i];
            if (i < size - 1) joinedProtocols += ",";
        }
    }

    Helper::getInstance()->send(NetCmd::Open(this));

    return true;
}

void WebSocketImpl::close()
{
    Helper::getInstance()->netThread->emit("close", NetCmd::Close(this));

}

void WebSocketImpl::closeAsync()
{
    Helper::getInstance()->netThread->emit("close", NetCmd::Close(this));

}

void WebSocketImpl::send(const char *data, size_t len)
{
    Helper::getInstance()->netThread->emit("send", NetCmd::Write(this, data, len, true));
}

void WebSocketImpl::send(const std::string &msg)
{
    Helper::getInstance()->netThread->emit("send", NetCmd::Write(this, msg.data(), msg.length, false));
}

int WebSocketImpl::lwsCallback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, ssize_t len)
{
    int ret = 0;
    switch (reason)
    {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        ret = onConnected();
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        ret = onError("lws_client_connection_error");
        break;
    case LWS_CALLBACK_CLIENT_RECEIVE:
        ret = onReadable(in, (size_t)len);
        break;
    case LWS_CALLBACK_CLIENT_WRITEABLE:
        ret = onWritable();
        break;
    case LWS_CALLBACK_WSI_DESTROY:
        ret = onClosed();
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
        lwsl_warn("lws callback reason %d is not handled!", reason);
        break;
    }
    return ret;
}


void WebSocketImpl::connect()
{
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

    auto useSSL = true; //TODO calculate from url

    lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = lwsProtocols == nullptr ? Helper::getInstance()->lwsDefaultProtocols : lwsProtocols;
    info.gid = -1;
    info.uid = -1;
    info.user = this;
    info.ssl_ca_filepath = caFile.c_str();

    info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS |
        LWS_SERVER_OPTION_LIBUV |
        LWS_SERVER_OPTION_PEER_CERT_NOT_REQUIRED;
    //ssl flags
    int sslFlags = LCCSCF_ALLOW_SELFSIGNED |
        LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK |
        LCCSCF_ALLOW_EXPIRED;

    if (useSSL)
    {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        sslFlags |= LCCSCF_USE_SSL;
    }

    lwsHost = lws_create_vhost(Helper::getInstance()->lwsContext, &info);

    if (useSSL)
    {
        lws_init_vhost_client_ssl(&info, lwsHost); //
    }


    struct lws_client_connect_info cinfo;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.context = Helper::getInstance()->lwsContext;
    cinfo.address = "invoke.top";
    cinfo.port = 6789;
    cinfo.ssl_connection = sslFlags;
    cinfo.path = "/";
    cinfo.host = "invoke.top";
    cinfo.origin = "invoke.top";
    cinfo.protocol = joinedProtocols.empty() ? "" : joinedProtocols.c_str();
    cinfo.ietf_version_or_minus_one = -1;
    cinfo.userdata = this;
    cinfo.client_exts = exts;
    cinfo.vhost = lwsHost;

    lwsObj = lws_client_connect_via_info(&cinfo);

    if (lwsObj == nullptr)
        onError("lws_client_connect_via_info() return nullptr");

    Helper::getInstance()->updateLibUV();
}

void WebSocketImpl::write(NetDataPack &pack)
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

    size_t bytesWrite = lws_write(lwsObj, pack.payload(), frameSize, (lws_write_protocol)writeProtocol);

    if (bytesWrite < 0)
    {
        //error 
        closeAsync();
    }
    else if (bytesWrite < frameSize)
    {
        pack.consume(bytesWrite);
    }
}

int WebSocketImpl::onError(const std::string &msg)
{
    std::cout << "connection error: " << msg << std::endl;
    Helper::getInstance()->runInUI([this]() {
        this->_delegate->onError(*(this->ws), -1); //FIXME error code
    });
    return 0;
}

int WebSocketImpl::onConnected()
{
    std::cout << "connected!" << std::endl; 
    Helper::getInstance()->runInUI([this]() {
        this->_delegate->onConnected(*(this->ws)); 
    });
    return 0;
}

int WebSocketImpl::onClosed()
{
    Helper::getInstance()->runInUI([this]() {
        this->_delegate->onDisconnected(*(this->ws));
    });
    return 0;
}

int WebSocketImpl::onReadable(void *in, size_t len)
{
    std::cout << "readable : " << len << std::endl;
    if (in && len > 0) {
        _receiveBuffer.insert(_receiveBuffer.end(), (uint8_t*)in, (uint8_t*)in + len);
    }
    
    auto remainSize = lws_remaining_packet_payload(lwsObj);
    auto isFinalFrag = lws_is_final_fragment(lwsObj);

    if (remainSize == 0 && isFinalFrag)
    {
        auto rbuffCopy = std::make_shared<std::vector<uint8_t>>(std::move(_receiveBuffer));
        
        _receiveBuffer.reserve(WS_REVERSED_RECEIVE_BUFFER_SIZE);

        bool isBinary = (lws_frame_is_binary(lwsObj) != 0);

        Helper::getInstance()->runInUI([rbuffCopy, this, isBinary]() {
            WebSocket::Data data(rbuffCopy->data, rbuffCopy->size(), isBinary);
            this->_delegate->onData(*(this->ws), data);
        });
    }
    return 0;
}

int WebSocketImpl::onWritable()
{
    std::cout << "writable" << std::endl;

    //TODO handle close

    //pop sent packs
    while (_sendBuffer.size() > 0 && _sendBuffer.front()->remain() == 0)
    {
        _sendBuffer.pop_front();
    }

    if (_sendBuffer.size() > 0)
    {
        auto &pack = _sendBuffer.front();
        if (pack->remain() > 0) {
            write(*pack);
        }
    }

    if (lwsObj && _sendBuffer.size() > 0)
        lws_callback_on_writable(lwsObj);

    return 0;
}


