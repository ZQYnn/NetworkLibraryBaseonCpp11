#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include <functional>
#include <memory>

// 头文件只使用类型的声明， 没有类型的具体的定义以及方法， 只写类型的前置声明即可
class EventLoop;
class Timestamp;

class Channel : noncopyable{
public:
    // 使用c++ 11的方式设置回调函数 
    
    using EventCallback  = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;
    
    /* 
    c 的方式进行 设置回调函数
    typedef std::function<void()> EventCallback; 
    typedef std::function<void(Timestamp)> ReadEventCallback;
    typedef std::
    */
    Channel(EventLoop *loop, int fd);
    ~Channel();
    
    
    // 处理事件 调用相应的方法 
    void handleEvent(Timestamp receiveTime);

    
    // 设置回调函数
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb ) { writeCallback_ = std::move(cb); } 
    void setCloseCallback(EventCallback cb ) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb)  { errorCallback_ = std::move(cb); } 



    // 8 mins 智能指针的问题  这里为什么使用 智能指针
    void tie(const std::shared_ptr<void> &);
    int fd() const {return fd_; }
    int events() const {return events_; }

    void  set_revents(int revt) { revents_ = revt; }
    

    // 这里update 是epoll_ctl 操作 将读事件添加到epoll 
    void enableReading() {events_ |= kReadEvent; update();}
    void disalbeReading() {events_ &= ~kReadEvent; update();}
    void enableWriting() {events_ |= kWriteEvent; update();} 
    void disableWriting() {events_ &= ~kWriteEvent; update();}
    void disableAll() { events_ = kNoneEvent; update();}
    
    
    
    bool isNoneEvent() const {return events_ == kNoneEvent; }
    bool isWriting() const {return events_ & kWriteEvent; }
    bool isReading()  const {return events_ & kReadEvent; }
    
    
    int index() {return index_;}
    void set_index(int idx ) {index_ = idx;}

    // one loop per thread 
    EventLoop* ownerLoop() {return loop_;}
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);
    
    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;
    

    EventLoop *loop_; 
    const int fd_;  // fd, Poller 监听的对象 
    int events_;    // 需要监听的的事件
    int revents_;   // poller 返回的已经发生的事件 根据对应的事件执行对应的回调函数
    int index_;     
    
    
    /*
        智能指针的问题， 使用weak—ptr  监听资源的状态， 弱指针 -> 强指针 
    */ 

    std::weak_ptr<void> tie_; // 使用weak_ptr 的原因 防止调用remove:channel
    bool tied_; 
    
    // Channel 负责获取对应的revents 事件, 所以它负责具体事件的回调。
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

 