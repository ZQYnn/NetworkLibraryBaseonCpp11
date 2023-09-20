#pragma once 

#include "noncopyable.h"
#include "Timestamp.h"
#include <vector>
#include <unordered_map> 


class Channel;
class EventLoop;

// muduo 库中多路事件分发器的核心IO复用模块
class Poller : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;
    Poller(EventLoop *loop);
    virtual ~Poller() = default;
    
    // 给所有IO复用保留统一接口
    virtual Timestamp poll(int timeoutMs, ChannelList  *activeChannels) = 0;

    //
    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;


    /// @brief  判断参数Channel中是否存存在poller当中
    virtual bool hasChannel(Channel* channel) const;
    
    
    // Eventloop 可以通过该接口获取默认的IO复用的具体实现
    static Poller* newDefaultPoller(EventLoop* loop);
protected: 
    /**
     * map key: fd, value:  sockfd 所属的通道类型
     * 通过fd 使用map 快速找到fd 对应的Channel封装， epoll 返回的就是对应的fd
    */
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;
private:
    EventLoop *ownerLoop_;
}; 

