#include "Thread.h"
#include "CurrentThread.h"

#include <semaphore.h>

std::atomic_int Thread::numCreated_(0);


Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false)
    , joined_(false)
    , tid_(0)
    , func_(std::move(func))
    , name_(name)
{
    setDefaultName();
}

Thread::~Thread()
{
    // 设置守护线程 主线程结束， 守护线程自动结束， 不存在孤儿线程的情况 
    if (started_ && !joined_)
    {
        // 设置分离线程
        thread_->detach();
        
    }
}

void Thread::start() 
{
    started_ = true;
    sem_t sem;
    sem_init(&sem, false, 0);
    // thread_ = std::shared_ptr<std::thread>)(new std::thread(出入lambda表达 ))
    // [&]() ->void{ }
    // &访问外部对象任意的成员变量
    thread_ = std::shared_ptr<std::thread>(new std::thread([&](){
        tid_ = CurrentThread::tid();
        // 保证获取获取线程ID
        sem_post(&sem);
        func_(); 
    }));
    
    sem_wait(&sem);
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if (name_.empty())
    {
        char buf[32];
        snprintf(buf, sizeof buf, "Thread%d", num);
        name_ = buf;
    }
}