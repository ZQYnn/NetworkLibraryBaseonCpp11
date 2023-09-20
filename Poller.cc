#include "Poller.h"
#include "Channel.h"

Poller::Poller(EventLoop *loop): ownerLoop_(loop)
{
    
}

bool Poller::hasChannel(Channel *channel) const
{
    auto it = channels_.find(channel->fd());
    return  it != channels_.end() &&  it->second == channel;
}

//关于newDefaultPoller的实现问题    为什么不在Poller中 直接实现newDefualtPoller 函数
// 而去专门 写一个DefaultPoller ？  poller ： 25 mins 