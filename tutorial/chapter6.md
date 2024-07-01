#  相关概念解释

## 项目概述

本项目实现一个基于非阻塞 IO、事件驱动以及 Reactor 设计模式的 C++ 高并发 TCP 网络库，本项通过 one loop per thread 线程模型实现高并发。

## 什么是 one loop per thread 以及选择它的原因

在此中模型下， 程序里的每个IO线程有一个eventloop （Reactor）用于处理事件的读写

- 一个线程内只包含一个eventloop（Reactor）
- 在一个eventLoop（Reactor）中可以监听多个fd， 其中每一个fd只能被当前线程进行读写操作， 以免在多线程中发生线程安全的问题

Libev 作者

> One loop per  thread is  usually  a good model. Doing this is almost never wrong, sometimes a better-performance model exists, but it is always a good start.

使用oneLoop per thread 好处是：

- 线程的数量基本固定，在程序启动的时候设定， 不会频繁创建和销毁。
- 可以很方便的在各个线程之间进行负载调配
- IO事件发生的线程基本是固定不变的，不必考虑TCP连接事件的并发（即fd读写都是在同一个线程进行的，不是A线程处理写事件B线程处理读事件）



## Reactor模式

> Reactor是这样的一种模式， 它要求主线程只负责监听文件描述符上是否有事件发生，有的话就立即将该事件通知工作线程。 除此之外，主线程不做任何其他实质性的工作。 读写数据，接受新的连接都是以及处理客户端请求均在工作线程中完成。
>
> ​								—— 《Linux高性能服务器编程》 p128

《Linux中高性能服务器编程》 书中提到的是**单Reactor多线程**模型， 模型处理事件的特点：

- Reactor 通过 select/poll/epoll （IO 多路复用接口） 监听事件，收到事件后通过 dispatch 进行分发，具体分发给 Acceptor 对象还是 Handler 对象，还要看收到的事件类型。
- 如果是连接建立的事件，则交由 Acceptor 对象进行处理，Acceptor 对象会通过 accept 方法 获取连接，并创建一个 Handler 对象来处理后续的响应事件。
- 如果不是连接事件，交给当前连接对应的Handler响应。


<img src ="https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/oneReactor.png" width=85%>


**单Reactor**结构处理事件的流程图

<img src="https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/Reactor03.png" width=85%>



## Muduo中的Reactor模式

而在muduo网络库中， 使用的**多Reactor多线程模型**,  这中模型更加高效，处理事件的思路如下

1. 客户端建立发送建立连接请求，在主线程的MainReactor通过`epoll`持续监听lfd上建立连接事件， 服务器端（`TcpServer`）收到请求，通过Acceptor建立连接后，执行相应回调（`TcpServer::newConnection`）, 将新的连接分配给子线程的SubReactor。
2. MainReactor只负责**监听客户端建立连接**请求以及**将新连接分配给子线程**，而SubReactor负责将MainReactor分配的连接加入到`epoll`监听connfd上对应的读写事件请求。
3. 如果SubReactor中有事件发生， SubReactor调用当前的Handler执行相应回调函数。

多Reactor多线程模型结构图：


<img src = "https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/multiReactor.png" width=85%>

**总结**：在Muduo中， `MainReactor` 只负责监听建立连接，通过`accept`将监听返回的`connfd`打包的`channel`上， 用轮询的方式，分发给`subReactor`，`subReactor`  对应的EventLoop 中的一个子线程处理相应事件， 工作线程上的`SubReactor` 代表一个`EventLoop`， 每个`EventLoop` 监听一组`Channel`， 每一组`Channel`都在自己的EventLoop线程中执行。

了解相关muduo中`多Reactor多线程模型`后，再来看看`TcpServer` 的完成[执行流程](./chapter5.md)，你就会理解其中思想。



## 网络库的IO模型是怎么？为什么这个IO模型是高效的？

来自小林[Coding](https://zhuanlan.zhihu.com/p/368089289)

设置非阻塞

```c++
int flag = fcntl(connect_fd, F_GETFL);
flag |= O_NONBLOCK;
fcntl(connect_fd, F_SETFL, flag);
```



**阻塞 I/O**，当用户程序执行 `read` ，线程会被阻塞，一直等到内核数据准备好，并把数据从内核缓冲区拷贝到应用程序的缓冲区中，当拷贝过程完成，`read` 才会返回。

注意，**阻塞等待的是「内核数据准备好」和「数据从内核态拷贝到用户态」这两个过程**。过程如下图：

<img src="https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/blocking.png" width=60%>

**非阻塞 I/O**，非阻塞的 read 请求在数据未准备好的情况下立即返回，可以继续往下执行，此时应用程序不断轮询内核，直到数据准备好，内核将数据拷贝到应用程序缓冲区，`read` 调用才可以获取到结果。过程如下图：

<img src="https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/non_blocking.png" width=60%>

`Reactor[one loop per thread: non-blocking + IO multiplexing]`模型。muduo采用的是Reactors in thread有一个main Reactor负责accept(2)连接，然后把连接挂在某个sub Reactor中(muduo中采用的是round-robin的方式来选择sub Reactor)，这样该连接的所有操作都在那个sub Reactor所处的线程中完成。多个连接可能被分到多个线程中，以充分利用CPU。





## 使用eventFd的作用是什么

- eventfd是linux的一个系统调用，为事件通知创建文件描述符，eventfd()创建一个“eventfd对象”，这个对象能被用户空间应用用作一个**事件等待/响应机制**，靠内核去响应用户空间应用事。
- 实现唤醒，让IO线程从IO multiplexing阻塞调用中返回，更高效地唤醒。







## ET 和LT 解释

ET：**服务器端只会从 epoll_wait 中苏醒一次**， 保证一次从内核缓冲区读完数据

LT：**服务器端不断地从 epoll_wait 中苏醒，直到内核缓冲区数据被 read 函数读完才结束**





## 为什么muduo中使用 LT模式呢 ？

最后是 muduo 为什么选择 LT 模式？一是 LT 模式编程更容易，并且不会出现漏掉事件的 bug，也不会漏读数据。二是对于 LT 模式，如果可以将数据一次性读取完，那么就和 ET 相同，也只会触发一次读事件，另外LT 模式下一次性读取完数据只会调用一次 read()，而 ET 模式至少需要两次，因为 ET 模式下 read() 必须返回 EAGAIN 才可以。写数据的情景也类似。三是在文件描述符较少的情况下，epoll 不一定比 poll 高效，使用 LT 可以于 poll 兼容，必要的时候可以切换为 PollPoller。



## muduo中是如何处理线程调用









#  执行流程梳理

完成网络库编写后， 查看`example`下的测试用例,  以echoserver作为案例， 讲解整个muduo库的工作流程

```c++
#include <mymuduo/TcpServer.h>
#include <mymuduo/Logger.h>
#include <string>
#include <functional>

class EchoServer
{
public:
    EchoServer(EventLoop *loop,
            const InetAddress &addr, 
            const std::string &name)
        : server_(loop, addr, name)
        , loop_(loop)
    {
        // 注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1)
        );

        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
        );

        server_.setThreadNum(3);
    }
    void start()
    {
        server_.start();
    } 
private:
    // 连接建立或者断开的回调
    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
        }
    }
    
    // 可读写事件回调
    void onMessage(const TcpConnectionPtr &conn,
                Buffer *buf,
                Timestamp time)
    {
        std::string msg = buf->retrieveAllAsString();
        conn->send(msg);
        conn->shutdown(); // 关闭写端  EPOLLHUP =》 closeCallback_
    }
    
    EventLoop *loop_;
    TcpServer server_;
};

int main()
{
    EventLoop loop;
    InetAddress addr(8000);
    EchoServer server(&loop, addr, "EchoServer-01"); 
    server.start(); 
    loop.loop();
    return 0;
}
```



## 执行过程

以`echoserver.cc` 为例，仔细分析网络库底层时如何运行起来的， 如何实现基于多线程 实现epoll 复用的，在底层时如何实现Reactor模式的。

## 1.server初始化



初始化echoserver时，完成对TcpServer和 EventLoop的构造。

```c++
EchoServer(EventLoop *loop,
        const InetAddress &addr, 
        const std::string &name)
    : server_(loop, addr, name)
    , loop_(loop)
```

## 2.TcpServer 构造



在构造TcpServer执行的操作

`TcpServer.cc`:

```c++
TcpServer::TcpServer(EventLoop *loop,
                    const InetAddress &listenAddr,
                    const std::string &nameArg,
                    Option option)
                    : loop_(CheckLoopNotNull(loop))
                    , ipPort_(listenAddr.toIpPort())
                    , name_(nameArg)
                    , acceptor_(new Acceptor(loop, listenAddr, option == kReusePort))
                    , threadPool_(new EventLoopThreadPool(loop,  name_))
                    , connectionCallback_()
                    , messageCallback_()
                    , nextConnId_(1)
                    , started_(0)
{
    acceptor_->setNewConncetionCallback(std::bind(&TcpServer::newConncetion, 
    this, std::placeholders::_1,  std::placeholders::_2));
}
```

在构造TcpServer时，创建Acceptor对象用于创建lfd，用于监听事件客户端请求，建立客户端与服务器端的连接，创建`EventLoopThreadPool`对象， 通过多线程处理IO复用请求。



**进行事件绑定**：在`Acceptor`中，通过`acceptSocket_` 创建`lfd` `acceptChannel_`封装`lfd`， Acceptor在mainReactor中，负责监听是否有客户端向服务器端请求建立连接，所以在acceptChannel中绑定读事件`Acceptor::handleRead()`。 

`Acceptor.cc` :

```c++
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (newConnectionCallback_)
        {
             newConnectionCallback_(connfd, peerAddr); 
        }
        else
        {
            ::close(connfd);
        }
    }
}
```

在`Acceptor::handleRead()`中主要执行两件事

- 获取connfd  `int connfd = acceptSocket_.accept(&peerAddr);` 
- 执行建立建立连接的回调函数`newConnectionCallback_`

`newConnectionCallback_`回调函数是在TcpServer构造的时候设置的。

`TcpServer::TcpServer()`:  

 Acceptor中，`newConnectionCallback_`绑定的是`TcpServer::newConnection`

```c++
acceptor_->setNewConncetionCallback(std::bind(&TcpServer::newConncetion, 
    this, std::placeholders::_1,  std::placeholders::_2));
```

以上完成TcpServer的构造，为监听客户端建立连接做好了铺垫。当开启事件循环时，出现连接的时候，执行newConnection函数。

## 3.启动echoServer服务器

在谈完整的启动过程时，先来回顾一下在Muduo中应用到的**多Reactor多线程**模型

<img src = "https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/multiReactor.png" width=85%>



在`echoserver `测试代码中  启动服务器。 

```c++
server.start(); ->   server_.start(); //TcpServer.start();
```

在启动TcpServer需要执行哪些操作？

`TcpServer.cc`

```c++
void TcpServer::start()
{
    if (started_++ == 0)
    {
        threadPool_->start(threadInitCallback_); 
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}
```

**在TcpServer启动的时候只做了两件事情**

- 启动线程池`threadPool_->start(threadInitCallback_) `

- 开启监听事件 执行`Acceptor::listen` 

     

**启动线程池**

 `EventLoopThreadPool.cc`

```c++
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
```



**开启事件监听**

`Acceptor.cc` 

```c++
void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();
    acceptChannel_.enableReading(); // 开启监听
}
```

**剖析更改事件监听流程** :在Channel 中保存了`event`代表需要监听的事件，将channel.event 传入`epoll_ctl`即可监听事件。 修改其他Channel感兴趣事件同理。

**执行的流程**： `acceptChannel_.enableReading()` -> 

**Channel.cc**

`enableReading() {events_ |= kReadEvent; update();}`  --> 

` update() {loop_->updateChannel(this)}`-->

**EventLoop.cc**

`updateChannel(Channel *channel){ poller_->updateChannel(channel)` -->

**EpollPoller.cc**

`updateChannel()` --> `update()` 最终执行`epoll_ctl`

```c++
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    //memset(&event, 0, sizeof event);
    bzero(&event, sizeof event);
    int fd = channel->fd();
    event.data.fd = fd;
    event.events = channel->events();
    event.data.ptr = channel;
    
    // 设置相应event事件 
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error %d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error %d\n", errno);
        }
    }
}
```



## 4.开启事件循环

开启事件循环

在开启线程池和设置监听事件之后， 直接开启事件循环, 这里启动的是`mainReactor`， 监听客户端建立连接事件。 

`echoserver.cc`

```c++
loop.loop(); // 启动mainLoop的底层Poller
```

`EventLoop.cc`

```c++
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    
    LOG_INFO("EventLoop %p start looping\n", this);
    while (!quit_)
    {
        activeChannels_.clear();
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_); 
        for (Channel *channel :  activeChannels_)
        {
            channel->handleEvent(pollReturnTime_);
        }
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p \n", this); 
    looping_ = false;   
}
```

loop中的重点：`pollReturnTime_ = poller_->poll()` poll 底层调用`epoll_wait`方法,监听客户端建立连接的请求（当前是在MainReactor中 ，只有acceptrChannel 绑定lfd监听客户端建立请求）

`EpollPoller.cc`

```c++
Timestamp EPollPoller::poll (int timeoutMs, ChannelList *activeChannels)
{ 
    int numsEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
 
    int saveErrno = errno; 
    
    Timestamp now(Timestamp::now());
    
    return now;
}
```

此时开启`poller`进行监听lfd上读事件， 等待客户端建立连接的请求。

执行到此，已经完成在MainReactor的任务， 即已经完成了下图中 <font color = red>红色</font>圈线部分操作，Reactor执行图如下：



<img src = "https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/muduo_reactor01.png" width=85%>


## 5.建立新连接

完成MainReactor上的accept事件后， 当前要做的就是将TcpConnection分发给`SubReactor`过程如下图红线所展示：

<img src = "https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/muduo_reactor02.png" width=85%>

监听acceptChannel上是否有事件发生， 需要执行AcceptChannel在`handleRead`中的`TcpServer::newConnection` 回调函数， 建立连接。

```c++
// listenfd 有事件发生了，  出现新用户连接
void Acceptor::handleRead()
{
    // 默认构造
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (newConnectionCallback_)
        {
             newConnectionCallback_(connfd, peerAddr); 
             // 轮询找到subLoop，唤醒，分发当前的新客户端的Channel
        }
        else
        {
            ::close(connfd);
        }
    }
}
```

接受客户端，在分发给subloop之前建立新连接建立连接`newConnection()`

```c++
void TcpServer::newConncetion(int sockfd, const InetAddress &peerAddr)
{
    // 使用轮询算法， 选择一个subloop 来管理channel
    EventLoop *ioLoop = threadPool_->getNextLoop();
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_); 
    ++nextConnId_; 
    std::string connName = name_ + buf;
    
    sockaddr_in local;
    ::bzero(&local, sizeof local);
    socklen_t addrlen = sizeof local;
    if (::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }
    InetAddress localAddr(local);
    
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
    
    conncetions_[connName] = conn;
     
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)
    );
    
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}
```



## 6 唤醒SubReactor



在多Reactor模型多线程模型中， MainReactor已经完成了在主线程中监听客户端连接，并且将TcpConnection分发给SubReactor，那么如何让SubReactor启动并执行相应的事件呢？

在回答这个问题之前，我们需要认识到muduo上存在的一个现象— **跨线程调用**

解释跨线程调用的问题，我们不得不回到首次出现跨线程调用的地方 — **`TcpServer::newConnection`**

执行`newConnection`有以下几个步骤：

- 获取ioLoop  `EventLoop *ioLoop = threadPool_->getNextLoop();`
- 创建TcpConnection对象
- 绑定subChannel相关回调函数
- 执行`TcpConntion::connectEstablished`,  `ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn))`

在MainLoop中调用`threadPool_->getNextLoop` ，获取ioLoop，这就是获取的指向subReactor的指针，那么在主线程中执行子Loop上 的相关回调函数是否可行呢？ 答案是否定的， 在muduo库中遵循了 one loop per thread原则，**即一个eventLoop对应一个子线程，eventLoop执行相关函数需要在属于自己所在的线程中执行**。

那么下面这句话就属于跨线程调用的：

```c++
ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
```

那在muduo中是如何解决跨线程调用的呢？ — `wakeupFd`

回到跨线程调用这句话，在看到`wakeupFd`之前，先剖析在内部是如何调用，在mainLoop中获取subLoop(ioLoop)，当前loop不在自己所在的线程，从而执行`queueInLoop()`

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

`EventLoop::queueInLoop`: 将回调函数`TcpConncetion::connectionEsatblished`保存到`pendingFunctors`中, 当前Loop不属于自己所在线程执行`wakeup()`

```c++
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
         pendingFunctors_.emplace_back(cb);
    }
    if(!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup(); // 满足 !isInLoopThread()-> 执行wakeup()
    }
}
```

`EventLoop.cc`: 唤醒loop所对应的线程， 执行相应回调函数`TcpConncetion::connectionEsatblished`

```c++
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes inside of 8\n", n); 
    } 
}
```

执行`wakeup()`函数， 到这里你可能有疑问，在`wakeup()`中不就是只有一个写操作么？ 那是如何唤醒的呢？

在一开始完成构造线程池对象的时候，已经创建多个eventLoop，每个eventLoop开启了loop方法

```c++
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    while (!quit_)
    {
        activeChannels_.clear();
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_); 
        for (Channel *channel :  activeChannels_)
        {
            channel->handleEvent(pollReturnTime_);
        }
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p \n", this); 
    looping_ = false;   
}
```

`EventLoop.loop()`方法本质上 就是执行  `poller_->poll()`内部`epoll_wait`方法 ，在`TcpConnection::newConnection`完成注册之前，在subLoop的`connfd`监听不到任何事件的,那么就导致子线程阻塞在`epoll_wait`这里，而在EventLoop上执行相应的事件是需要调用 `doPendingFunctors()`的， 目前就出现了在注册TcpConnection的过程中设置回调`TcpConncetion::connectionEsatblished`无法执行， 出现这就现象我们就需要**唤醒操作**

唤醒方法：让EventLoop不要阻塞在poll函数， 运行到`doPendingFunctors()`,就可以完成任务，具体的实现方法如下

1. 每个EventLoop上设置了`wakeupFd_`, 将`wakeupFd_`封装成`wakeupChannel_`
2. 在构造EventLoop对象的时候在Poller上注册可读事件
3. 在`EventLoop::queueInLoop()` 中调用`wakeup()` 通过`wakeupFd_`写数据，`epoll_wait` 检测到可读事件， 解除阻塞问题，进而执行`doPendingFunctors`



`doPendingFunctors` 上执行的是`TcpConnection::ConnectionEstablished`

```c++
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    // 强智能指针保证TcpConnection不被释放,
    channel_->tie(shared_from_this()); 
    // 向poller注册channel的epollin事件 也就是读事件
    channel_->enableReading();
    // 新连接建立 执行回调函数
    connectionCallback_(shared_from_this());
}
```

connectionCallback是用户自己在echoserver中绑定的OnConncetion函数，此时已经唤醒SubReactor并完整地注册好连接。

目前，我们已经完整梳理好唤醒subReactor的来龙去脉：当出现**跨线程调用**的时候，通过`wakeup()`**唤醒**EventLoop，执行相应回调即可，同理出现其他的跨线程调用的情况执行代码逻辑相同。

## 7. subLoop 处理 读写数据

在完成注册TcpConnection之后，着重处理SubReactor上的读写事件，即处理当前红色圈线部分内容

<img src = "https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/muduo_reactor03.png" width=85%>

poller中监听sub各种事件 ，TcpConncetion构造时绑定subChannel各种事件回调函数，

```c++
TcpConnection::TcpConnection(EventLoop *loop,
                const std::string &nameArg,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr)
                : loop_(CheckLoopNotNull(loop))
                , name_(nameArg)
                , state_(kconnecting) 
                , reading_(true)
                , socket_(new Socket(sockfd))
                , channel_(new Channel(loop, sockfd))
                , localAddr_ (localAddr)
                , peerAddr_(peerAddr)
                , highWaterMark_(64 * 1024 * 1024)
                
{
    // 设置Channel的回调函数 poller 给channel 通知对应事件发生， channel执行相应的回调
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this)
    );
    
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this)
    );
    
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this)
    );

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
                    
}
```



发生读事件的时候，执行handleRead

```c++
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime); 
    }
    else  if (n == 0)
    {
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection heandleRead");
        handleError();
    }
}
```



下图说明整个网络库的执行流程 ：
<img src = "https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/muduo.drawio.png" width=85%>





​     

​     

​     

​     
