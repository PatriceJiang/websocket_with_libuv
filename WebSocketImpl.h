#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <libwebsockets.h>

#include "WebSocket.h"

class NetDataPack;

class WebSocketImpl : public std::enable_shared_from_this<WebSocketImpl> 
{
private:
    static int _protocolCounter;
    static std::atomic_int64_t _wsIdCounter;
public:
    typedef std::shared_ptr<WebSocketImpl> Ptr;

    static std::unordered_map<int64_t, Ptr > _cachedSocketes;

    WebSocketImpl(WebSocket *);
    virtual ~WebSocketImpl();

    bool init(const std::string &uri, WebSocketDelegate::Ptr delegate, const std::vector<std::string> &protocols, const std::string &caFile);
    void sigClose();
    void sigCloseAsync();
    void sigSend(const char *data, size_t len);
    void sigSend(const std::string &msg);

    int lwsCallback(struct lws *wsi, enum lws_callback_reasons reason, void*, void*, ssize_t);

private:
    void doConnect();
    void doDisconnect();    //callbacks
    void doWrite(NetDataPack &pack);

    int netOnError(WebSocket::ErrorCode code);
    int netOnConnected();
    int netOnClosed();
    int netOnReadable(void *, size_t len);
    int netOnWritable();

public:
    WebSocketDelegate::Ptr _delegate;
    WebSocket *_ws = nullptr;
    WebSocket::State _state;
private:
    std::string _uri;
    std::string _caFile;
    std::vector<std::string> _protocols;
    std::string _joinedProtocols = "";
    std::vector<uint8_t> _receiveBuffer;
    //libwebsocket fiels
    lws *_wsi = nullptr;
    lws_vhost *_lwsHost = nullptr;
    lws_protocols *_lwsProtocols = nullptr;
    int64_t _wsId;

    std::list<std::shared_ptr<NetDataPack>> _sendBuffer;

    friend class Helper;
};