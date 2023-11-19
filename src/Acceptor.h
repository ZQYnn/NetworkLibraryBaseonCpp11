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


    // 重点
    void handleRead();
    // baseLoop
    EventLoop *loop_;

    // 相当于lfd 在cc文件中 实现了createNonBlocking 中返回lfd
    Socket acceptSocket_;
    Channel acceptChannel_;
    
    //  连接成功 tcpserver 选择subLoop
    // connfd 打包为channel  获取loop
    NewConnectionCallback newConnectionCallback_;
    bool listenning_;
    int nextConnId_;
}; 