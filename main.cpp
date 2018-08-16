
#include <string>
#include <iostream>
#include <chrono>
#include <ctime>

#include <vector>
#include <memory>
#include <thread>

#include "WebSocket.h"
#include "Looper.h"

typedef Looper<std::string> LooperString;
LooperString::Ptr strLooper;

class WSDelegate : public WebSocketDelegate
{
public:
    virtual void onConnected(WebSocket &so) override {
        std::cout << "[WSDelegate] " << "onConnnected!~" << std::endl;
        so.send("hello plutoo");

        strLooper->run();
    }
};

static void dump_string(char *p, int len) {
    char buff[200] = { 0 };
    snprintf(buff, 200, "bytes.len.%d-", len);
    int unit_len = strlen(buff);
    int initialized = 0;
    if (len <= unit_len) {
        strncpy(p, buff, len);
        return;
    }
    //init 
    strncpy(p, buff, unit_len);
    initialized = unit_len;
    while (initialized < len)
    {
        int count = initialized < len - initialized ? initialized : len - initialized;
        strncpy(p + initialized, p, count);
        initialized *= 2;
    }
}


class Ticker : public Loop
{
private:
    int idx = 0;
    WebSocket *_ws = nullptr;
public:
    Ticker(WebSocket *ws) : _ws(ws) {}
    virtual void update(int dtms) override
    {
        int count = 100 + 1000 * idx;
        idx += 1;
        count = count > (1024 * 80) ? 1024 * 80 : count;
        char *buff = (char *)malloc(count + 1);
        dump_string(buff, count);
        buff[count] = 0;
        _ws->send(buff);
        free(buff);
    }
};

int main(int argc, char **argv)
{
    WebSocket *ws = new WebSocket();

    Ticker * ticker = new Ticker(ws);
    strLooper = std::make_shared<LooperString>(ticker, 2000);

    ws->init("", std::make_shared<WSDelegate>(), std::vector<std::string>(), "");
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    

    //ws->close();
    system("pause");
    delete ticker;
    delete ws;
    return 0;
}