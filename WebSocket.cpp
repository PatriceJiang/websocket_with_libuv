#include "WebSocket.h"

#include "WebSocketImpl.h"

#include <iostream>
#include <vector>
#include <string>

WebSocket::WebSocket() { impl = std::make_shared<WebSocketImpl>(); }
WebSocket::~WebSocket() { impl.reset(); }

bool WebSocket::init(const std::string &uri, WebSocketDelegate::Ptr delegate, const std::vector<std::string> &protocols, const std::string &caFile)
{
    return impl->init(uri, delegate, protocols, caFile);
}

void WebSocket::close() { impl->close(); }

void WebSocket::send(const std::string &msg) { impl->send(msg); }

void WebSocket::send(const char *data, size_t len) { impl->send(data, len); }


//////////////default delegate impl///////////////


void WebSocketDelegate::onConnected(WebSocket &ws)
{
    std::cout << "Websocket " << "connected!" << std::endl;
}

void WebSocketDelegate::onDisconnected(WebSocket &ws)
{
    std::cout << "Websocket " << "disconnected!" << std::endl;
}

void WebSocketDelegate::onError(WebSocket &ws, int errCode)
{
    std::cout << "Websocket " << "onError " << errCode << std::endl;
}

void WebSocketDelegate::onData(WebSocket &ws, const char *data, int len)
{
    std::cout << "Websocket " << "recieve data" << len << " bytes !" << std::endl;
}