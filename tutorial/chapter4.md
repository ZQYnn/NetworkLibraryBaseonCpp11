# TcpConnection

在功能上来说 ，TcpConnection和Acceptor是平行关系， TcpConnection主要负责建立连接之后SubReactor绑定各类回调函数， 负责处理相关事件， 控制建立连接和断开方法 以及Tcp连接服务端和客户端的套接字地址信息等。

## 主要成员

```c++
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

Buffer inputBuffer_;
Buffer outputBuffer_; 
```

## 成员方法

```c++

// 给SubReactor 设置相应读写回调函数
void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }
void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }

void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
{ 
    highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark;
}


// 绑定底层的channel的不同事件， 
// 在handleXXX中执行TcpConnection中绑定的回调函数
void handleRead(Timestamp receiveTime); 
void handleWrite();
void handleClose();
void handleError();
void sendInLoop(const void *message, size_t len); 
void shutdownInLoop(); 
```



# TcpServer

TcpServer  将之前实现的模块功能整合到TcpServer中， Acceptor、Socket、EventLoopThreadPool，TcpConnection。 使用TcpServer类完成整个通信流程。



中运行了 Acceptor，Acceptor构造得到lfd， 封装为acceptChannel，通过mainLoop注册到poller中， poller 监听acceptChannel上发生的事件， 如果存在事件，accpetChannel执行相应的回调函数， acceptChannel(readCallback)绑定的是handleRead,  handleRead主要执行的是在TcpServer中绑定的newconnection 回调函数， 

listenfd  bind setReadCallback -> handleRead -> newConnectionCallback -> acceptor->setnewconnectionCallback -> TcpServer::newConnection

(TcpServer.start() -> loop_ -> runInLoop(Acceptor::listen)  -> enableReading() , ->  channel . update -> loop.updateChannel-> poller.updatechannl -> epoll_ctl)



(EventLoop.loop)  loop.loop -> 将EventLoop中ChannelList 执行相应的回调函数，此时建立连接





```c++
EventLoop loop;
InetAddress addr(8000);
EchoServer server(&loop, addr, "EchoServer-01"); // Acceptor non-blocking listenfd  create bind 
server.start(); // listen  loopthread  listenfd => acceptChannel => mainLoop =>
loop.loop(); // 启动mainLoop的底层Poller
return 0;
```





之后创建ThreadPool对象， 



newConnection主要执行的任务是：

根据轮训算法， 选择一个subLoop， 唤醒subLoop， 把当前connfd 封装成Channel分发给subLoop， 

细节部分

1. 在mainLoop中获取subloop （ EventLoop *ioLoop = threadPool_->getNextLoop();）

2. 之后绑定各种回调函数

3.   ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));

4. 执行EventLoop.runInLoop queInLoop

     ```c++
     void EventLoop::runInLoop(Functor cb)
     {
         if (isInLoopThread())  
             cb();
        
         else            
         {
             queueInLoop(cb); // 执行这里
         }
     }
     ```

5. queueInLoop 满足条件wakeup

     ```c++
     void EventLoop::queueInLoop(Functor cb)
     {
         {
             std::unique_lock<std::mutex> lock(mutex_);
              pendingFunctors_.emplace_back(cb);
         }
         if(!isInLoopThread() || callingPendingFunctors_)
         {
             wakeup(); // 满足!isInLoopThread()-> 执行wakeup()
         }
     }
     ```

6. establishedConnection（onConnection）

     ```c++
     // 建立连接 TcpConnection 管理channel
     void TcpConnection::connectEstablished()
     {
         setState(kConnected);
         // 强智能指针保证TcpConnection不被释放,
         channel_->tie(shared_from_this()); 
         // 向poller注册channel的epollin事件 也就是读事件
         channel_->enableReading();
     
         // 新连接建立 执行回调函数 onConnection 设置的回调
         connectionCallback_(shared_from_this());
     }
     ```

7. 建立连接之后subReactor出现读事件 相应上层的 onMessage 

     ```c++
     channel_ ->setReadCallback(Tcp::handleRead);
     void TcpConnection::handleRead
     {
       read();
       messageCallback()
     }
     ```

8. 连接关闭时，怎样处理关闭的 handleClose， closeCallback

     ```c++
     void TcpConnection::handleClose()
     {
         LOG_INFO("fd = %d state = %d \n", channel_->fd(),(int)state_);
         setState(kDisconnected);
         channel_->disableAll();
         //  tcp 关闭连接
         TcpConnectionPtr  connptr(shared_from_this());
         connectionCallback_(connptr); // 执行关闭连接的回调的
         closeCallback_(connptr);      // 关闭连接的回调函数 执行的是TcpServer::removeConnection回调方法
     }
     ```

     



## 主要成员



```c++
// baseLoop;
EventLoop *loop_; 
const std::string ipPort_;
const std::string name_;

std::unique_ptr<Acceptor> acceptor_;
std::shared_ptr<EventLoopThreadPool> threadPool_;

ConnectionCallback connectionCallback_; // 有新连接时的回调
MessageCallback messageCallback_; // 有读写消息时的回调
WriteCompleteCallback writeCompleteCallback_; // 消息发 送完成以后的回调
ThreadInitCallback threadInitCallback_; // loop线程初始化的回调
std::atomic_int started_; 

int nextConnId_;            
ConnectionMap conncetions_; //保存 所有的连接 
```

## 成员函数

启动TcpServer服务器

```c++
void TcpServer::start()
{
    if (started_++ == 0)
    {
        // 启动底层的loop线程池 
        threadPool_->start(threadInitCallback_); 
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}
```



建立新连接时 绑定新连接

```c++
void TcpServer::newConncetion(int sockfd, const InetAddress &peerAddr)
{
    // 使用轮询算法， 选择一个subloop 来管理channel
    EventLoop *ioLoop = threadPool_->getNextLoop();
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_); 
    ++nextConnId_; 
    std::string connName = name_ + buf;
    LOG_INFO("TcpServer::newConnection [%s] - new conncetion [%s] from %s \n",
            name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());
    
    // 通过sockfd 获取其绑定的本机的ip地址和端口号 
    sockaddr_in local;
    ::bzero(&local, sizeof local);
    socklen_t addrlen = sizeof local;
    if (::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }
    InetAddress localAddr(local);
    
    
    // 根据连接成功的sockfd 创建TcpConnection连接对象
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
    
    conncetions_[connName] = conn;
     
    // 下面的回调函数都是 用户设置给Tcpserver -> tcpConnection -> Channel -> poller ->  channel 进行回调
    //  用户自己设置的回调函数
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    

    // 设置如何关闭回调  用户调用shutdown
    // conn(tcpConnection) -> shutdown ->shutdownInLoop -> sockct::shutWrite -> 
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)
    );
    
    // 直接调用TcpConnection::connectEstablished -> onConnection(用户设置的)
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}
```





