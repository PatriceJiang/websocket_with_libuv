
#include <string>
#include <iostream>
#include <chrono>
#include <ctime>

#include <vector>
#include <memory>
#include <thread>

#include "WebSocket.h"
#include "Looper.h"

class WSDelegate : public WebSocketDelegate
{
public:
    virtual void onConnected(WebSocket &so) override {
        std::cout << "[WSDelegate] " << "onConnnected!~" << std::endl;
        so.send("hello plutoo");
    }
};

int main(int argc, char **argv)
{
    WebSocket *ws = new WebSocket();
    ws->init("", std::make_shared<WSDelegate>(), std::vector<std::string>(), "");

    std::this_thread::sleep_for(std::chrono::seconds(5));
    //ws->close();
    //delete ws;
    system("pause");
    return 0;
}