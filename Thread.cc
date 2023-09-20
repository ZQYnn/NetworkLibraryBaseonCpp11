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
        // thread 提供了设置分离线程的方法
        thread_->detach();
        
    }
}
// 启动线程
void Thread::start() // thread 对象记录的就是新线程的详细信息
{
    started_ = true;
    sem_t sem;
                // pshared 进程共享 设置false 
    sem_init(&sem, false, 0);
    // lambda 表达式的写法 没有掌握好 传入线程函数
    // 开启线程
    thread_ = std::shared_ptr<std::thread>(new std::thread([&](){
        tid_ = CurrentThread::tid();
        // 信号量资源 + 1
        sem_post(&sem);
        func_(); // 开启一个新的线程， 专门执行该线程函数
    }));
    // 等待获取上面新创建的线程
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