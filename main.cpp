
#include <string>
#include <iostream>
#include <chrono>
#include <ctime>
#include "Looper.h"

class SimpleLoop :public Loop
{
public:
    void before() {
        std::cout << "before" << std::endl;
    }
    void after()
    {
        std::cout << "after" << std::endl;
    }
    void update(int dtms)
    {
        auto now = time(nullptr);
        std::cout << "update " << now << std::endl;
    }
};

int main(int argc, char **argv)
{
 
    std::shared_ptr<Loop> tsk(new SimpleLoop);

    {
        auto loop = std::make_shared<Looper<std::string> >(ThreadCategory::NET_THREAD, tsk, 1000LL);
        loop->run();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200000));

    return 0;
}