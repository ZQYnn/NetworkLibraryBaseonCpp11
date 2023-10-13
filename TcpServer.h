#pragma once

#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "EventLoopThreadPool.h" 
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

#include <functional>
#include <memory>
#include <atomic>
#include <unordered_map>

class TcpServer : noncopyable
{
public:
    // 启动一个loop线程
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    enum Option
    {
        kNoReusePort,
        kReusePort, 
    };
    TcpServer(EventLoop *loop, 
            const InetAddress &listenAddr,
            const std::string &nameArg,
            Option option = kNoReusePort);
    ~TcpServer();
    
    void setThreadInitcallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setConnectionCallback(const ConnectionCallback &cb) {connectionCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb;}
    
    void setThreadNum(int numThreads);
    
    void start(); 
private:
    void newConncetion(int sockfd, const InetAddress &peerAddr);
    void removeConnection(const TcpConnectionPtr &conn);
    void removeConnectionInLoop(const TcpConnectionPtr &conn);
    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    
    // baseLoop;
    EventLoop *loop_; 
    const std::string ipPort_;
    const std::string name_;
    // 运行在mainLoop
    std::unique_ptr<Acceptor> acceptor_;
    // one loop per thread;  
    std::shared_ptr<EventLoopThreadPool> threadPool_;

    ConnectionCallback connectionCallback_; // 有新连接时的回调
    MessageCallback messageCallback_; // 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成以后的回调
    
    ThreadInitCallback threadInitCallback_; // loop线程初始化的回调
    std::atomic_int started_; 
    
    int nextConnId_;
    ConnectionMap conncetions_; //保存 所有的连接
}; 
