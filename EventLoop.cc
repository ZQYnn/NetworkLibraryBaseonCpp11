#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <functional>
#include <errno.h>
#include <memory>


/*
防止一个线程创建多个EventLoop
一个线程中创建一个EventLoop之后，对t_loopInThisThread 进行标记
如果t_loopInThisThread 不为空则说明已经创建过EventLoop对象
*/
// 
// __thread 是Thread local 机制 不添加的话则所有的线程共享
__thread EventLoop *t_loopInThisThread = nullptr;

// io复用接口超时的时间
const int kPollTimeMs = 100000;


// 调用系统接口  用notify的唤醒subReactor处理新来的Channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error: %d\n", errno);
    }
    return evtfd;
}


// EventLoop  具体逻辑
EventLoop::EventLoop() 
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
    , currentActiveChannel_(nullptr)
     
{
    LOG_DEBUG("EVentLoop created %p in thread %d\n", this,  threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", t_loopInThisThread, threadId_);
        
    }
    else 
    {
        t_loopInThisThread = this;
    }
    // 绑定器和 函数对象 不是很理解WakeChannel的作用和用法
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    wakeupChannel_->enableReading();
    
}


EventLoop::~EventLoop()
{
    wakeupChannel_->disalbeAll();
    wakeupChannel_->remove(); 
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
    
}

void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    
    LOG_INFO("EventLoop %p start looping\n", this);
    while (!quit_)
    {
        activeChannels_.clear();
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_); 
        for (Channel *channel :  activeChannels_)
        {
            //  poller 监听那些channel发生的事件， 之后上报给EventLoop， 通知Channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        //执行EventLoop事件循环的需要处理的回调操作
        
        /*
        IO 线程， mianLoop 接受新用户的连接 accept，得到相应fd 经过Channel 打包， 通过wakeup 唤醒subloop
        mainLoop 事先注册一个回调cb（需要subloop来执行）
        唤醒subloop后， 执行下边的方法， 执行之前mainloop注册的cb操作
        */ 
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p \n", this); 
    looping_ = false;
    
}

void EventLoop::quit()
{
    
    quit_ = true;
    // 自己线程中调用quit， 正常退出
    // 如果在其他线程中调用 quit方法， 在subloop 中， 调用了mainLoop的quit
    // 依然还是3 - 22 mins
    if (!isInLoopThread())
    {
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())  // 在当前loop线程中执行cb
    {
        cb();
    }
    else            //  在非当前loop线程中执行cb
    {
        queueInLoop(cb);
    }
}

// 这个函数还是不太理解  是用std::
void EventLoop::queueInLoop(Functor cb)
{
    {
        // 这是为什么使用lock
        std::unique_lock<std::mutex> lock(mutex_);
        // push_back 是拷贝构造 而emplace_back是直接构造
        pendingFunctors_.emplace_back(cb);
        
    }
    // 唤醒相应的 需要执上面回调操作的loop线程  
    // callingPendingFunctors 含义 ？ 
    // 当亲的loop正在执行回调 但是loop又添加的了新的回调函数
    if(!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();
    }
}

void EventLoop::handleRead()
{
    uint64_t one = 1; // long int 8 bytes
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %lu bytes inside of 8\n", n);
        // 
    }
}


// 用来唤醒loop所在的线程的 想wakeupfd_ 写一个数据
// wakeupChannel发生读事件， 当前 线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes inside of 8\n", n); 
        
    }
    
}
     
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
    
}

// 执行回调  
void EventLoop::doPendingFunctors()
{
    // 这里为什么需要多设置一个vector向量   eventLoop 4 17mins
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_); 
    }
    for (const Functor &functor : functors)
    {
        // 当前loop 需要执行的回调操作
        functor();
    }
    callingPendingFunctors_ = false;
}