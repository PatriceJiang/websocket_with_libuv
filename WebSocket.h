#pragma once

#include <string>
#include <memory>
#include <vector>

class WebSocketDelegate;
class WebSocketImpl;

class WebSocket {
public:
    struct Data {
        Data();
        Data(char *bytes, size_t len, bool isBinary) :bytes(bytes), len(len), isBinary(isBinary) 
        {}
        char *bytes = nullptr;
        size_t len = 0;
        size_t isused = 0;
        bool isBinary;
        void *ext;
    };
    
    enum class State
    {
        CONNECTING,
        OPEN,
        CLOSING,
        CLOSED
    };

public:
    WebSocket();
    virtual ~WebSocket();

    bool init(const std::string &uri, std::shared_ptr<WebSocketDelegate>  delegate, const std::vector<std::string> &protocols, const std::string &caFile);
    void close();
    void send(const char *data, size_t len);
    void send(const std::string &msg);

private:
    std::shared_ptr<WebSocketImpl> impl;
};

class WebSocketDelegate
{
public:
    typedef std::shared_ptr<WebSocketDelegate> Ptr;
    virtual void onConnected(WebSocket &ws);
    virtual void onDisconnected(WebSocket &ws);
    virtual void onError(WebSocket &ws, int errCode);
    virtual void onData(WebSocket &ws, const WebSocket::Data &data);
};
