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

// __thread 是Thread local 机制 不添加的话则所有的线程共享
// 防止一个线程创建多个eventLoop 
__thread EventLoop *t_loopInThisThread = nullptr;

// io复用接口超时的时间
const int kPollTimeMs = 100000;


// 调用系统接口  用notify的唤醒subReactor处理新来的Channel
// 通知subEventLoop
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error: %d\n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop() 
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)  
    , threadId_(CurrentThread::tid()) // 获取当前线程ID
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd()) // 创建eventFd作为线程间通信机制
    , wakeupChannel_(new Channel(this, wakeupFd_)) // wakeupFd封装为Channel
    , currentActiveChannel_(nullptr) // 当前活跃的channel
     
{
    LOG_DEBUG("EVentLoop created %p in thread %d\n", this,  threadId_);
    // 已经保存了thread的值，说明之前已经创建过了EventLoop
    // 在一个线程中只能创建一个EventLoop，退出当前构造
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", t_loopInThisThread, threadId_);
    }
    else 
    {
        t_loopInThisThread = this;
    }
    
    // 设置 wakeupChannel读事件的回调函数
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    wakeupChannel_->enableReading();
}


EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
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
        // 这里的poll 主要是两种fd
        // 一种是wakeupfd，main和sub 之间唤醒使用，一种是clientFd  客户端通信使用
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_); 
        for (Channel *channel :  activeChannels_)
        {
            // poller 监听那些channel发生的事件， 之后上报给EventLoop， 通知Channel处理相应的事件
            
            channel->handleEvent(pollReturnTime_);
        }
        //执行EventLoop事件循环的需要处理的回调操作
        
        /*
        IO 线程， mianLoop 接受新用户的连接 accept，得到相应fd 经过Channel 打包， 通过wakeup 唤醒subloop
        mainLoop 事先注册一个回调cb（需要subloop来执行）
        唤醒subloop后， 执行下边的方法， 执行之前mainloop注册的cb操作
        */ 
        /*
        IO线程 mainLooper ->acceptor fd (经过channel打包）通过wakeup传递给subloop        
        mainLoop， 设置回调 需要subLoop执行
        */
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p \n", this); 
    looping_ = false;   
}


// loop在自己的线程中调用quit
// 在自己的线程中 
void EventLoop::quit()
{
    
    quit_ = true;
    // 自己线程中调用quit， 正常退出
    
    // 如果在其他线程中调用 quit方法， 在subloop 中， 调用了mainLoop的quit
    // 依然还是3 - 22 mins
    // 其他线程subloop中调用quit，调用mainLoop 中的quit 退出epoll
    /*
        subloop1    subloop2
    
    */ 
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

void EventLoop::queueInLoop(Functor cb)
{
    {
        // 这是为什么使用lock
        std::unique_lock<std::mutex> lock(mutex_);
        // push_back 是拷贝构造 而emplace_back是直接构造
         pendingFunctors_.emplace_back(cb);
        
    }
    // 当前的线程 执行回调函数时添加回调，此时就需要再次weakeup
    if(!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();
    }
}

// 唤醒 相应的loop， 通过wakeupChannel
void EventLoop::handleRead()
{
    uint64_t one = 1; // long int 8 bytes
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %lu bytes inside of 8\n", n);
        // 8 bytes   
    }
}


// 用来唤醒loop所在的线程的 想wakeupfd_ 写一个数据
// wakeupChannel发生读事件， 当前 线程就会被唤醒
// mainReactor 向sub 写消息， 执行， 从而唤醒对应线程， 

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes inside of 8\n", n); 
        
    } 
}
     

// EventLoop  中的poller方法 
void EventLoop::updateChannel(Channel *channel){ poller_->updateChannel(channel); }
void EventLoop::removeChannel(Channel *channel){ poller_->removeChannel(channel); }
bool EventLoop::hasChannel(Channel *channel){ return poller_->hasChannel(channel); }




//注册的回调是  tcpServer向这里传pendingFunctors
void EventLoop::doPendingFunctors()
{
    // 这里为什么需要多设置一个vector向量
    // 将成员Functor放到局部中 // 提高并发效率， 
    // pendingFunctor中存在大量的回调函数， 一次执行效率比较低
    
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