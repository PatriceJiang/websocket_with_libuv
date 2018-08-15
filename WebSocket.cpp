#include "WebSocket.h"

#include "WebSocketImpl.h"

#include <iostream>
#include <vector>
#include <string>

WebSocket::WebSocket() { impl = std::make_shared<WebSocketImpl>(this); }
WebSocket::~WebSocket() { impl.reset(); }

bool WebSocket::init(const std::string &uri, WebSocketDelegate::Ptr delegate, const std::vector<std::string> &protocols, const std::string &caFile)
{
    return impl->init(uri, delegate, protocols, caFile);
}

void WebSocket::close() { impl->sigClose(); }

void WebSocket::emit(const std::string &msg) { impl->sigSend(msg); }

void WebSocket::emit(const char *data, size_t len) { impl->sigSend(data, len); }


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

void WebSocketDelegate::onMesage(WebSocket &ws, const WebSocket::Data &data)
{
    std::cout << "Websocket " << "recieve data" << data.len << " bytes !" << std::endl;
}