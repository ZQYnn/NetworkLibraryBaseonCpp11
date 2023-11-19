#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"

#include <memory>



EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string& nameArg)
    : baseLoop_(baseLoop)
    , name_(nameArg)
    , started_(false)
    , numThreads_(0)
    , next_(0)

{
    
}
    
EventLoopThreadPool::~EventLoopThreadPool()
{
    
}

//   
void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;
    
    for (int i = 0; i < numThreads_; i++)
    {
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%d", name().c_str(), i);
        EventLoopThread* t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        
        // 底层创建线程 绑定一个新的EventLoop 返回Loop的地址 
        loops_.push_back(t->startLoop());
    }

    // numThreads == 0 说明整个服务端只有一个线程， 运行着baseLoop
    if (numThreads_ == 0 && cb)
    {
        cb(baseLoop_);
    }
}


EventLoop* EventLoopThreadPool::getNextLoop()
{
    EventLoop *loop = baseLoop_;
    // 通过轮询获取下一个事件处理的loop
    if (!loops_.empty())
    {
        loop = loops_[next_];
        ++next_;
        if (next_ >= loops_.size())
        {
            next_ = 0;
        }
    }
    return loop;
}
// 
std::vector<EventLoop*> EventLoopThreadPool:: getAllLoops()
{
    if (loops_.empty())
    {
        return std::vector<EventLoop*> (1, baseLoop_);
    }
    else
    {
        return loops_;
    }
}

