#pragma once
#include "noncopyable.h"
#include <functional>
#include <string>
#include <vector>
#include <memory>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    EventLoopThreadPool(EventLoop *baseLoop, const std::string& nameArg);
    ~EventLoopThreadPool();
    
    void setThreadNum(int numThreads) { numThreads_ = numThreads; }
    void start(const ThreadInitCallback &cb = ThreadInitCallback());
    
    EventLoop* getNextLoop();
    // 如果是多线程的话 baseLoop 默认使用轮训的方式分配Channel给subloop
    std::vector<EventLoop*> getAllLoops();

    bool started() const { return started_; }
    
    const std::string& name()const {return name_;}
    
    
    
private:
    //  用户创建的loop
    EventLoop *baseLoop_;
    std::string name_;
    bool started_; 
    int numThreads_;
    int next_;
    // 所有事件的线程
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    // 所有事件线程的eventLoop指针
    std::vector<EventLoop*> loops_;
};