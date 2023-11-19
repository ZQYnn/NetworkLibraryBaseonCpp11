#pragma once

#include "Poller.h"
#include "Timestamp.h"
//  出现继承 或者使用的对应类中具体的方法 应当使用include 包含头文件
//  如果只是使用对应的类作为参数 或者 引用， 直接 使用类声明用即可


/*
epoll_create  ->EpollPoller -> constructor  destructor
epoll_ctl -> update  
epoll_wait -> poll 
*/
#include <vector>
#include <sys/epoll.h>
class EPollPoller : public Poller
{
public:
    EPollPoller(EventLoop *loop);
    ~EPollPoller() override; // cpp 11
    Timestamp poll (int timeoutMs, ChannelList *activeChannels) override;
    void updateChannel(Channel *channel) override;
    void removeChannel(Channel *channel) override;
private: 
    static const int kInitEventListSize = 16;
    // 填写活跃的连接
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
    // 更新channel通道
    void update(int operation, Channel *channel);
    
    
    using EventList = std::vector<epoll_event>;
    int epollfd_;
    EventList events_;
};