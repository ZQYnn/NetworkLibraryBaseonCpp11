#pragma once
#include <functional>
#include <vector>
#include <atomic>
#include <memory> // 智能指针 
#include <mutex>  // c++ 11 锁机制
#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

class Channel;
class Poller;
// 事件循环  主要包含 channel poller (epoll抽象)

class EventLoop : noncopyable
{
public:
    // 设置回调函数
    using Functor = std::function<void()>;    

    EventLoop();
    ~EventLoop();

    void loop();           // 开启事件循环
    void quit();           // 退出当前的事件循环
    
    Timestamp pollReturnTime() const {return pollReturnTime_;} 
    
    
    void runInLoop(Functor cb);     // 在当前的loop中执行cb
    void queueInLoop(Functor cb);    // 将cb放入队列中 唤醒loop所在的线程 执行cb
    

    // mainReactor 唤醒 subReactor 
    void wakeup();                  // 唤醒loop所在的线程
    
    void updateChannel(Channel *channel);   // EventLoop中的方法 
    void removeChannel(Channel *channel); 
    bool hasChannel(Channel *channel);

    // 判断当前EventLoop 对象是否在创建它自己的线程中 
    bool isInLoopThread() const {return threadId_ == CurrentThread::tid();}
    
private:
    void handleRead();          // wake up
    void doPendingFunctors();   // 执行回调
    

    // epollPoller 经过poll 操作之后 返回的events 绑定相应的channel
    // 最后保存为ChannelList 类型
    using ChannelList =  std::vector<Channel*>;
    
    // 
    std::atomic_bool looping_;              // 原子操作
    std::atomic_bool quit_;                 // 标识loop是否退出循环
    
    const pid_t threadId_;                  //  当前线程的ID， 判断

    // 定义Timestamp 类型变量 需要包含头文件
    // 编译时需要确定变量的大小
    // 如果定义 引用 或者指针 可以用类声明
    Timestamp pollReturnTime_;              // 返回发生事件的时间  EpollPoller中 poll 返回的时间
    // 是用智能指针的原因是什么？
    std::unique_ptr<Poller> poller_;
    
    //****  mainLoop获取一个新用户的Channel， 通过轮询算法选择一个subloop 通过该成员唤醒subloop处理Channel
    // 使用 eventFd创建出来的  int eventfd(unsigned int initval, int flag);
    int wakeupFd_;  
    std::unique_ptr<Channel> wakeupChannel_;  //wakeupChannel 打包了wakeupfd 
    
    ChannelList activeChannels_;            // 保存EventLoop下的所有channel 经过poll
    Channel *currentActiveChannel_;

    std::atomic_bool callingPendingFunctors_;//当前loop 是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_; //存储loop需要执行的所有的回调操作
    std::mutex mutex_;  // 互斥锁 用来保护上边vector线程安全操作 
    
};  
