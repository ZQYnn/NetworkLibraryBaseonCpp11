#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

// constructor
// EventLoop中包含了 ChannelList 以及 poller 包含了这两种
Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
    {
        
        
    }

Channel::~Channel()
{

    
}

// 智能指针的使用
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

/*
重点： 通过所属的EventLoop ,调用poller相应的方法，注册fd的events事件
*/
void Channel::update()
{
    loop_->updateChannel(this);
    
}

//在Channel 所属的EventLoop中，把当前的Channel删除
void Channel::remove()
{
    loop_->removeChannel(this);   
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        // 弱智能指针-> 强智能指针
        // 智能指针的
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            //
            handleEventWithGuard(receiveTime);
        }
    }
     else
    {
        handleEventWithGuard(receiveTime);
        
    }
}

/// @brief 根据poller通知的channel发生的具体事件， 由channel负责调用的具体的回调操作
/// @param receiveTime 
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel  handleEvent revents:%d\n", revents_);
    
    if ((revents_ & EPOLLHUP) && ! (revents_ & EPOLLIN))
    {
        if (closeCallback_)
        {
            closeCallback_();
        }
    }

    if (revents_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }    
    }

    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }
    



    
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
            
        }
    }
    
}