#include "Poller.h"
#include "EPollPoller.h"
#include <stdlib.h>


Poller* Poller::newDefaultPoller(EventLoop *loop){
    if (::getenv("MUDUO_USE_POLL")){
        return nullptr; // 生成poll 对应的实例
    }else{
        return new EPollPoller(loop);  // 生成epoll 对应的实例
    }
}