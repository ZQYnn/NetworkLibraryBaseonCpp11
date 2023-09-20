#include "EventLoopThread.h"
#include "EventLoop.h"
#include <functional>


EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
        const std::string &name)
        : loop_(nullptr)
        , exiting_(false)
        , thread_(std::bind(&EventLoopThread::threadFunc, this), name)
        , mutex_()
        , cond_()
        , callback_(cb)
        
{
            
}
EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        // 
        thread_.join();
    }
}

EventLoop * EventLoopThread::startLoop()
{
    thread_.start();        // 启动底层线程thread中的func函数 底层线程的回调函数
    
    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while ( loop_ == nullptr)
        {
            cond_.wait(lock);
        }
        loop = loop_;
    }
    return loop;
}


// 
//下面这个方法:是在单独的新线程里面运行的
void EventLoopThread::threadFunc()
{
    // 创建一个独立的EventLoop， 和上面的线程是一一对应的 one loop per thread
    EventLoop loop;
    if (callback_)
    {
        callback_(&loop);
    }
    
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    loop.loop(); // EventLoop loop -> poller.loop
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}