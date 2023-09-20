#pragma once

#include "noncopyable.h"
#include <functional>
#include <thread>
#include <memory>
#include <unistd.h>
#include <string>
#include <atomic>
// 
class Thread : noncopyable
{
public:
    using ThreadFunc = std::function<void()>;
    explicit Thread(ThreadFunc, const std::string &name = std::string());

    ~Thread(); 
    void start();
    void join();

    bool started() const {return started_;}
    pid_t tid() const {return tid_;}

    const std::string& name() const {return name_;}
    static int numCreated() {return numCreated_;}
    
    
private:
    void setDefaultName();
    bool started_;
    bool joined_;
    // 不能直接使用 如果直接创建则线程直接启动 绑定线程函数
    // 需要智能指针控制线程启动的时机， 使用智能指针封装
    //std::thread thread_; 
    std::shared_ptr<std::thread> thread_;
    pid_t tid_;
    ThreadFunc  func_;
    std::string name_;
    static std::atomic_int numCreated_;
};