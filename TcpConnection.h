#pragma once
#include "noncopyable.h"
#include "memory"
#include "InetAddress.h"
#include "Buffer.h"
#include "Timestamp.h"
#include "Callbacks.h"
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;
class EventLoop;
/*
user -> tcpserver -> tcpconnection

channel 注册到poller中， poller 通知channel 最后channel调用相应的回调函数处理
tcpconnection -> channel -> poller

TcpServer -》 acceptor 有一个新用户连接，通过accept 函数得倒connfd
=》 TcpConnection 设置回调函数 -》 channel -》 poller

*/
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop *loop,
                const std::string &name,
                int sockfd,
                const InetAddress& localAddr_,
                const InetAddress& peerAddr_);
    ~TcpConnection();  

    EventLoop* getLoop() const {return loop_;}
    // 这个name 是干什么的啊？
    const std::string& name() const { return name_;}
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }

    bool connected() const {return state_ == kConnected; }
    
    void send(const std::string &buf);
    void shutdown();

    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }
    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }

    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
    { 
        highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark;
    }
   
    
    // 建立连接
    void connectEstablished(); 
    // 销毁连接  
    void connectDestoryed();
    
private:
    enum StateE {kDisconnected, kconnecting, kConnected, kDisconnecting};
    void setState(StateE state) { state_ = state; }
    void handleRead(Timestamp receiveTime); 
    void handleWrite();
    void handleClose();
    void handleError();
    
    
    void sendInLoop(const void *message, size_t len); 
    void shutdownInLoop(); 
   
    
    // 这里不是baseLoop， TcpConnection都是在subLoop中管理的
    EventLoop *loop_;
    const std::string name_;
    std::atomic_int state_;
    bool reading_;
    
    // 这里和acceptor类似 Acceptor -》 mainLoop 主要监听新用户的连接
    // TcpConnection => subLoop 监听已连接用户的读写事件
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;
    
    
    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    
    // 回调函数 执行的逻辑 tcpServer -> 通过Accepor 打包tcpConnection -> 
    // 注册到poller ->  返回给Channel-> channel 执行相应的回调函数
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;
    
    size_t highWaterMark_;
    
    // 读写buffer
    Buffer inputBuffer_;
    Buffer outputBuffer_; 
};