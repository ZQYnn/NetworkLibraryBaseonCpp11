# Acceptor



在前两部分中已经实现主要事件驱动部分， 在开始Reactor模型之前，对于每一个事件，不管提供什么样的服务，首先需要做的事都是调用`accept()`函数接受这个TCP连接，然后将socket文件描述符添加到epoll。当这个IO口有事件发生的时候，再对此TCP连接提供相应的服务。

![Reactor](./assets/Reactor.png)

## 主要成员

```c++
EventLoop *loop_; // 相当于mainLoop mainReactor 负责监听

Socket acceptSocket_;
Channel acceptChannel_; // 接受lfd() 处理监听事件

NewConnectionCallback newConnectionCallback_; 
// 成功建立连接返回给subLoop
```

## 成员函数

在创建TcpServer的时候 构造Acceptor 函数， 在acceptChannel 绑定handleRead

```c++
Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking())
    , acceptChannel_(loop, acceptSocket_.fd())
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr); 
    // tcpServer -> start  Acceptor listen  新用户连接 执行回调connfd -》channel-》 subloop
    // baseLoop -》 acceptChannel_(listened) -> 
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}
```

在handleRead中执行newConnectioncallback函数， newConnection在Tcp Server中实现

```c++
void Acceptor::handleRead()
{
    // 默认构造
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (newConnectionCallback_)
        {
            //在 tcpserver中 实现 具体
             newConnectionCallback_(connfd, peerAddr); 
             // 轮询找到subLoop，唤醒，分发当前的新客户端的Channel
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d accept err:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reached limit! \n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}
    
```





# Socket

Socket  主要是工具类， 主要实现Tcp建立连接API

```c++
#pragma once

#include "noncopyable.h"
class InetAddress;

class Socket : noncopyable
{
public:
    explicit Socket(int sockfd): sockfd_(sockfd)
    {} 
    ~Socket();
    int fd()const  {return sockfd_;}
    
    void bindAddress(const InetAddress &localaddr);
    void listen();
    int accept(InetAddress *peeraddr);
    void shutdownWrite();
    void setTcpNoDelay(bool on); 
    void setReuseAddr(bool on);
    void setKeepAlive(bool on);
    void setReusePort(bool on);
    
private:
    const int sockfd_;
};
```















