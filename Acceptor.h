#pragma once
#include "noncopyable.h"
#include "Socket.h"
#include "Channel.h"
#include "Logger.h" 

// 运行在mainloop中的 使用baseLoop
class Acceptor : noncopyable
{
public:

    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;
    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    
    ~Acceptor();
    void setNewConncetionCallback(const NewConnectionCallback &cb)
    {
        newConnectionCallback_ = cb;
    }
    bool listenning() {return listenning_;}
    void listen();
    
private :
    // 重点内容
    void handleRead();
    EventLoop *loop_;
    Socket acceptSocket_;
    Channel acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    bool listenning_;
}; 