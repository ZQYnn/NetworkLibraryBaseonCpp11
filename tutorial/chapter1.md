# Channel

Channel 对文件描述符和事件进行了一层封装。平常我们写网络编程相关函数，基本就是创建套接字，绑定地址，转变为可监听状态（这部分我们在 Socket 类中实现过了，交给 Acceptor 调用即可），然后接受连接。

但是得到了一个初始化好的 socket 还不够，我们是需要监听这个 socket 上的事件并且处理事件的。比如我们在 Reactor 模型中使用了 epoll 监听该 socket 上的事件，我们还需将需要被监视的套接字和监视的事件注册到 epoll 对象中。

Channel 将socket或accept返回的fd（lfd或connfd）进行封装，在channel中绑定监听事件， 经过epoll之后返回需要处理事件， 以及对应事件的回调函数。



## 成员变量

```c++
// 表示监听事件的状态
static const int kNoneEvent;
static const int kReadEvent;
static const int kWriteEvent;

EventLoop *loop_;   // 当前Channel属于的EventLoop
const int fd_;      // fd, Poller监听对象
int events_;        // 注册fd感兴趣的事件
int revents_;       // poller返回的具体发生的事件
int index_;         // 在Poller上注册的情况

std::weak_ptr<void> tie_;   // 弱指针指向TcpConnection(必要时升级为shared_ptr多一份引用计数，避免用户误删)
bool tied_;  // 标志此 Channel 是否被调用过 Channel::tie 方法

// 不同事件对应的回调函数
ReadEventCallback readCallback_; 	// 读事件回调函数
EventCallback writeCallback_;		// 写事件回调函数
EventCallback closeCallback_;		// 连接关闭回调函数
EventCallback errorCallback_;		// 错误发生回调函数
```

## 成员函数

主要的成员函数

```c++
// 设置需要监听的事件
void enableReading() {events_ |= kReadEvent; update();}
void disalbeReading() {events_ &= ~kReadEvent; update();}
void enableWriting() {events_ |= kWriteEvent; update();} 
void disableWriting() {events_ &= ~kWriteEvent; update();}
void disableAll() { events_ = kNoneEvent; update();}
// 通过poller 更新监听事件 
// channel.update -> eventloop.update -> poller.update
// poller 执行 epoll_CTL操作更新对应事件
void update();

//设置回调函数
void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
void setWriteCallback(EventCallback cb ) { writeCallback_ = std::move(cb); } 
void setCloseCallback(EventCallback cb ) { closeCallback_ = std::move(cb); }


// 根据当前事件和对应的回调函数， 执行相应回调函数
void handleEvent(Timestamp receiveTime);
void handleEventWithGuard(Timestamp receiveTime);
```



## Channel功能

channel主要绑定文件描述符、监听事件、回调函数并执行上层EventLoop返回的回调函数，用户可以自由注册各种事件、很方便地根据不同事件类型设置不同回调函数。

# Poller

在muduo网络库中，提供了Poller作为虚基类的，`EPollPoller`作为派生类， 其中`EpollPoller`主要实现了`epoll`作为IO复用的主要功能。如果需要实现其他IO复用功能，例如`Poll`或者`select`，继续编写一个派生类继承Poller即可。

## Poller设计

```c++
class Poller : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;
    Poller(EventLoop *loop);
    virtual ~Poller() = default;
    
    // 给所有IO复用保留统一接口
    virtual Timestamp poll(int timeoutMs, ChannelList  *activeChannels) = 0;

    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;


    /// @brief  判断参数Channel中是否存存在poller当中
    virtual bool hasChannel(Channel* channel) const;
    
    
    // Eventloop 可以通过该接口获取默认的IO复用的具体实现
    static Poller* newDefaultPoller(EventLoop* loop);
protected: 
    /**
     * map key: fd, value:  sockfd 所属的通道类型
     * 通过fd 使用map 快速找到fd 对应的Channel封装， epoll 返回的就是对应的fd
    */
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;
private:
    EventLoop *ownerLoop_;
}; 
```





## EpollPoller

EpollPoller作为Poller的派生类， 主要实现基类中的同名方法， 通过epoll方式完成IO复用。

使用epoll主要是调用三个API

- epoll_create
- epoll_ctl
- epoll_wait

实现这三个接口调用的分别是`EpollPoller  constructor (epoll_create) `  `updateChannel (epoll_ctl)`  `poll(epoll_wait)`在EpollPoller派生类的主要实现这三个成员函数。

```c++
#pragma once

#include "Poller.h"
#include "Timestamp.h"

/*
epoll_create  ->EpollPoller -> constructor  destructor
epoll_ctl -> update  
epoll_wait -> poll 
*/
#include <vector>
#include <sys/epoll.h>
class EPollPoller : public Poller
{
public:
    EPollPoller(EventLoop *loop);
    ~EPollPoller() override; 
    Timestamp poll (int timeoutMs, ChannelList *activeChannels) override;
    void updateChannel(Channel *channel) override;
    void removeChannel(Channel *channel) override;
private: 
    static const int kInitEventListSize = 16;
    // 填写活跃的连接
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
    // 更新channel通道
    void update(int operation, Channel *channel);
    
    using EventList = std::vector<epoll_event>;
    int epollfd_;
    EventList events_; // 作为传出参数 返回revent 
};
```



# EventLoop

EventLoop是事件驱动核心类，不管是接受客户端连接还是处理客户端事件，都是围绕epoll来编程，可以说epoll是整个程序的核心，服务器做的事情就是监听epoll上的事件，然后对不同事件类型进行不同的处理。这种以事件为核心的模式又叫事件驱动。

EventLoop 对应着事件循环，其驱动着 Reactor 模型。我们之前的 Channel 和 Poller 类都需要依靠 EventLoop 类来调用。

```c++
std::unique_ptr<Poller> poller_;

// scratch variables
ChannelList activeChannels_;
Channel* currentActiveChannel_;
```



其实 EventLoop 也就是 `Reactor`模型的一个实例，其重点在于循环调用 `epoll_wait` 不断的监听发生的事件，然后调用处理这些对应事件的函数。而这里就设计了线程之间的通信机制。

最初写`socket`编程的时候会涉及这一块，调用`epoll_wait`不断获取发生事件的文件描述符，这其实就是一个事件循环。

```c++
while (1)
{
    // 返回发生的事件个数
    int n = epoll_wait(epfd, ep_events, EPOLL_SIZE, -1); 

    // 这个循环的所有文件描述符都是发生了事件的，效率得到了提高
    for (i = 0; i < n; i++)
    {
        //客户端请求连接时
        if (ep_events[i].data.fd == serv_sock) 
        {
        	// 接收新连接的到来
        }
        else //是客户端套接字时
        {
        	// 负责读写数据
        }
    }
}
```



## EventLoop 成员变量



```cpp
using ChannelList = std::vector<Channel*>;
std::atomic_bool looping_;  // 原子操作，通过CAS实现，标志正在执行事件循环
std::atomic_bool quit_;     // 标志退出事件循环
std::atomic_bool callingPendingFunctors_; // 标志当前loop是否有需要执行的回调操作
const pid_t threadId_;      // 记录当前loop所在线程的id
Timestamp pollReturnTime_;  // poller返回发生事件的channels的返回时间
std::unique_ptr<Poller> poller_;

/**
 * TODO:eventfd用于线程通知机制，libevent和我的webserver是使用sockepair
 * 作用：当mainLoop获取一个新用户的Channel 需通过轮询算法选择一个subLoop 
 * 通过该成员唤醒subLoop处理Channel
 */
int wakeupFd_;  // mainLoop向subLoop::wakeupFd写数据唤醒
std::unique_ptr<Channel> wakeupChannel_;

ChannelList activeChannels_;            // 活跃的Channel，poll函数会填满这个容器
Channel* currentActiveChannel_;         // 当前处理的活跃channel
std::mutex mutex_;                      // 用于保护pendingFunctors_线程安全操作
std::vector<Functor> pendingFunctors_;  // 存储loop跨线程需要执行的所有回调操作
```

1. `wakeupFd_`：如果需要唤醒某个`EventLoop`执行异步操作，就向其`wakeupFd_`写入数据。
2. `activeChannels_`：调用`poller_->poll`时会得到发生了事件的`Channel`，会将其储存到`activeChannels_`中。
3. `pendingFunctors_`：如果涉及跨线程调用函数时，会将函数储存到`pendingFunctors_`这个任务队列中。





##EventLoop成员方法



### 判断是否跨线程调用 isInLoopThread()

![image.png](https://cdn.nlark.com/yuque/0/2022/png/26752078/1663324955126-3a8078fe-f271-4a1b-82c7-b75edff3cda8.png#averageHue=%23f4eed1&clientId=u6eef8e53-8892-4&crop=0&crop=0&crop=1&crop=1&from=paste&id=u40a6544b&margin=%5Bobject%20Object%5D&name=image.png&originHeight=435&originWidth=720&originalType=url&ratio=1&rotation=0&showTitle=false&size=309988&status=done&style=none&taskId=ucf757af1-9e42-4c5f-9efc-aea52ba8c53&title=)



muduo 是主从`Reactor`模型，主`Reactor`负责监听连接，然后通过轮询方法将新连接分派到某个从`Reactor`上进行维护。

1. 每个线程中只有一个`Reactor`
2. 每个`Reactor`中都只有一个`EventLoop`
3. 每个`EventLoop`被创建时都会保存创建它的线程值。
```cpp
bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
```
```cpp
// 以下是 CurrentThread::tid 的实现
namespace CurrentThread
{
    extern __thread int t_cachedTid; // 保存tid缓冲，避免多次系统调用
    
    void cacheTid();

    // 内联函数
    inline int tid()
    {
        if (__builtin_expect(t_cachedTid == 0, 0))
        {
            cacheTid();
        }
        return t_cachedTid;
    }
}
```
###  EventLoop 创建之初都做了什么？

```cpp
EventLoop::EventLoop()
  : looping_(false), // 还未开启任务循环，设置为false
    quit_(false),   // 还未停止事件循环
    eventHandling_(false), // 还未开始处理任务
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()), // 获取当前线程tid
    poller_(Poller::newDefaultPoller(this)), // 获取默认的poller
    timerQueue_(new TimerQueue(this)), // 定时器管理对象
    wakeupFd_(createEventfd()), // 创建eventfd作为线程间等待/通知机制
    wakeupChannel_(new Channel(this, wakeupFd_)), // 封装wakeupFd成Channel
    currentActiveChannel_(NULL) // 当前正执行的活跃Channel
{
    LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
    // 已经保存了thread值，之前已经创建过了EventLoop
    // 一个线程只能对应一个EventLoop，直接退出
    if (t_loopInThisThread)
    {
        LOG_FATAL << "Another EventLoop " << t_loopInThisThread
                    << " exists in this thread " << threadId_;
    }
    else
    {
        // 线程静态变量保存tid值
        t_loopInThisThread = this;
    }
    // 设置wakeupChannel_的读事件回调函数
    wakeupChannel_->setReadCallback(
        std::bind(&EventLoop::handleRead, this));
    // we are always reading the wakeupfd
    // 将wakeupChannel_注册到epoll，关注读事件
    // 如果有别的线程唤醒当前EventLoop，就会向wakeupFd_写数据，触发读操作
    wakeupChannel_->enableReading();
}
```
这里关注一下`eventfd`的创建，创建时指定其为非阻塞
```cpp
int createEventfd()
{
    int evfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evfd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evfd;
}
```
### EventLoop 销毁时的操作

```cpp
EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();   // channel移除所有感兴趣事件
    wakeupChannel_->remove();		    // 将channel从EventLoop中删除
    ::close(wakeupFd_);						  // 关闭 wakeupFd_
    t_loopInThisThread = nullptr;		// 指向EventLoop指针为空
}
```

1. 移除`wakeupChannel`要监视的所有事件
2. 将`wakeupChannel`从`Poller`上移除
3. 关闭`wakeupFd`
4. 将EventLoop指针置为空
###  EventLoop 事件驱动的核心——loop()

调用 EventLoop.loop() 正式开启事件循环，其内部会调用 `Poller::poll -> ::epoll_wait`正式等待活跃的事件发生，然后处理这些事件。

1. 调用 `poller_->poll(kPollTimeMs, &activeChannels_)` 将活跃的 Channel 填充到 activeChannels 容器中。
2. 遍历 activeChannels 调用各个事件的回调函数
3. 调用 `doPengdingFunctiors()`处理跨线程调用的回调函数
```cpp
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    
    LOG_INFO("EventLoop %p start looping\n", this);
    while (!quit_)
    {
        activeChannels_.clear();
        // 这里的poll 主要是两种fd
        // 一种是wakeupfd，main和sub 之间唤醒使用，一种是clientFd  客户端通信使用
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_); 
        for (Channel *channel :  activeChannels_)
        {
            // poller 监听那些channel发生的事件， 之后上报给EventLoop， 通知Channel处理相应的事件
            
            channel->handleEvent(pollReturnTime_);
        }
        //执行EventLoop事件循环的需要处理的回调操作
        
        /*
        IO 线程， mianLoop 接受新用户的连接 accept，得到相应fd 经过Channel 打包， 通过wakeup 唤醒subloop
        mainLoop 事先注册一个回调cb（需要subloop来执行）
        唤醒subloop后， 执行下边的方法， 执行之前mainloop注册的cb操作
        */ 
        /*
        IO线程 mainLooper ->acceptor fd (经过channel打包）通过wakeup传递给subloop        
        mainLoop， 设置回调 需要subLoop执行
        */
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p \n", this); 
    looping_ = false;   
}
```
### EventLoop 如何执行分派任务

EventLoop 使用 `runInLoop(Functor cb)`函数执行任务，传入参数是一个回调函数，让此 EventLoop 去执行任务，可跨线程调用。比如可以这么调用：
```cpp
ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
```
这是 `TcpServer`类的一处代码，其在接收了一个新连接后会创建一个对应的`TcpConnection`对象来负责连接。而`TcpConnection`是需要执行一些初始化操作的，这里就是让EventLoop执行`TcpConnection`的初始化任务。

我们继续看一下 `EventLoop::runInLoop` 的内部

```cpp
// 在I/O线程中调用某个函数，该函数可以跨线程调用
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())
    {
        // 如果是在当前I/O线程中调用，就同步调用cb回调函数
        cb();
    }
    else
    {
        // 否则在其他线程中调用，就异步将cb添加到任务队列当中，
        // 以便让EventLoop真实对应的I/O线程执行这个回调函数
        queueInLoop(std::move(cb));
    }
}
```
`isInLoopThread`判断本线程是不是创建该 EventLoop 的线程

1. 如果是创建该 EventLoop 的线程，则直接同步调用，执行任务
2. 如果不是，则说明这是跨线程调用。需要执行`queueInLoop`
### EventLoop 是如何保证线程安全的

还以上述的那个例子：
```cpp
EventLoop* ioLoop = threadPool_->getNextLoop();
ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
```
这里获取的 ioLoop 是从线程池中某个线程创建而来的，那么可以知道创建 ioLoop 的线程和目前运行的线程不是同一个线程，那么这个操作是线程不安全的。

一般为了保证线程安全，我们可能会使用互斥锁之类的手段来保证线程同步。但是，互斥锁的粗粒度难以把握，如果锁的范围很大，各个线程频繁争抢锁执行任务会大大拖慢网络效率。

而 muduo 的处理方法是，保证各个任务在其原有得线程中执行。如果跨线程执行，则将此任务加入到任务队列中，并唤醒应当执行此任务得线程。而原线程唤醒其他线程之后，就可以继续执行别的操作了。可以看到，这是一个异步得操作。
接下来继续探索`queueInLoop`的实现：

```cpp
// 将任务添加到队列当中，队就是成员pendingFunctors_数组容器
void EventLoop::queueInLoop(Functor cb)
{
    {
        // 操作任务队列需要保证互斥
        MutexLockGuard lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }

    /**
     * 调用此函数的线程不是这个EventLoop的创建线程
     * 或者正在处理PendingFunctors的情况则唤醒IO线程
     * 
     * 如果是当前的IO线程调用且并没有正处理PendgingFunctors
     * 则不必唤醒
     */    
    if (!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();
    }
}
```
`queueInLoop`的实现也有很多细节，首先可以看到在局部区域生成一个互斥锁（支持`RALL`），然后再进行任务队列加入新任务的操作。
这是因为可能此`EventLoop`会被多个线程所操纵，假设多个线程调用`loop->queueInLoop(cb)`，都向此任务队列加入自己的回调函数，这势必会有线程间的竞争情况。需要在此处用一个互斥锁保证互斥，可以看到这个锁的粒度比较小。

再往下，注意下方这个判断，`if (!isInLoopThread() || callingPendingFunctors_)`，第一个很好理解，不在本线程则唤醒这个 `EventLoop `所在的线程。第二个标志位的判断是什么意思呢？

`callingPendingFunctors_` 这个标志位在 `EventLoop::doPendingFunctors()` 函数中被标记为 true。 **也就是说如果 EventLoop 正在处理当前的 PendingFunctors 函数时有新的回调函数加入，我们也要继续唤醒。** 倘若不唤醒，那么新加入的函数就不会得到处理，会因为下一轮的 epoll_wait 而继续阻塞住，这显然会降低效率。这也是一个 muduo 的细节。
继续探索 `wakeup()` 函数，从其名字就很容易看出来，这是唤醒其他线程的操作。如何唤醒那个`EventLoop`的所在线程呢，其实只要往其 `wakeupFd_ `写数据就行。

每个`EventLoop`的`wakeupFd_`都被加入到`epoll`对象中，只要写了数据就会触发读事件，`epoll_wait `就会返回。因此`EventLoop::loop`中阻塞的情况被打断，`Reactor`又被事件「驱动」了起来。

```cpp
void EventLoop::wakeup()
{
    // 可以看到写的数据很少，纯粹是为了通知有事件产生
    uint64_t one = 1;
    ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
    }
}
```
这里可以看另一个例子，`TcpServer`的销毁连接操作。会在`baseLoop`中获取需要销毁的连接所在的`ioLoop/`，然后让`ioLoop`执行销毁操作，细节可以看注释。
```cpp
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  // 从map删除
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  // 获取subLoop
  EventLoop* ioLoop = conn->getLoop();
  /**
   * subLoop调用TcpConnection::connectDestroyed
   * 因为是在baseLoop的线程里调用subLoop的函数，所以不能同步调用，需要放入队列
   * 在加锁的环境下将此回调哈函数保存到subLoop的pendingFunctors_中并唤醒
   * 那么这个唤醒就是在baseLoop的线程里调用subLoop::wakeup写数据给subLoop的wakeFd,subLoop的主事件循环被唤醒执行pendingFunctors_
   * 而baseLoop线程在wakeup写完数据之后就没有继续往下执行了，这就保证整个函数只被subloop线程执行
   * 保证了线程的安全
   */
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
}
```
### EventLoop 是如何处理 pendingFunctors 里储存的函数的？

这里又是一处细节。我们为什么不直接遍历这个容器，而是又不嫌麻烦地定义了一个 functors 交换我们 pendingFunctors 的元素，然后遍历 functors？

我们如果直接遍历 pendingFunctors，然后在这个过程中别的线程又向这个容器添加新的要被调用的函数，那么这个过程是线程不安全的。如果使用互斥锁，那么在执行回调任务的过程中，都无法添加新的回调函数。这是十分影响效率的。

所以我们选择拷贝这个时候的回调函数，这样只需要用互斥锁保护一个交换的操作。锁的粗粒度小了很多。我们执行回调操作就直接遍历这个`functors`容器，而其他线程继续添加回调任务到 `pendingFunctors`。

```cpp
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    /**
     * 如果没有生成这个局部的 functors
     * 则需要一个粗粒度较大的互斥锁加，我们直接遍历pendingFunctors
     * 那么其他线程这个时候无法访问，无法向里面注册回调函数，增加服务器时延
     */
    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const Functor &functor : functors)
    {
        functor();
    }
	// 记录此时结束了处理回调函数
    callingPendingFunctors_ = false;
}
```
### 主动关闭事件循环时会发生什么？

```cpp
void EventLoop::quit()
{
    quit_ = true;

    /**
     * TODO:生产者消费者队列派发方式和muduo的派发方式
     * 有可能是别的线程调用quit(调用线程不是生成EventLoop对象的那个线程)
     * 比如在工作线程(subLoop)中调用了IO线程(mainLoop)
     * 这种情况会唤醒主线程
     */
    if (isInLoopThread())
    {
        wakeup();
    }
}
```

1. `quit_`置为 true
2. 判断是否是当前线程调用，若不是则唤醒 `EventLoop`去处理事件。

第二点可以深究一下，通过 `while(!quit_)` 可以判断，唤醒之后会继续处理一轮事件，然后再进入判断语句，然后因为 `quit_ = true` 而退出循环。

所以如果可以调用成功，说明之前的任务都处理完了，不会出现正在处理任务的时候突然退出了。但是不能保证未来事件发生的处理。

# 关系

以上内容，我们讨论了`Channel ` `Poller` `EventLoop` 细节， 可以总结他们之间的关系

1. Channel 负责封装文件描述符和其感兴趣的事件，里面还保存了事件发生时的回调函数， 执行相应回调函数。

2. Poller 负责`I/O`复用的抽象，派生类EpollPoller实现具体功能，其内部调用`epoll_wait`获取活跃的 Channel

3. EventLoop 相当于 Channel 和 Poller 之间的桥梁，Channel 和 Poller 之间并不之间沟通，而是借助着 EventLoop 这个类， 在EpollPoller中执行完成poll后，将Channel保存到activeChannels，执行每个Channel中需要处理的事件。

     ```c++
     pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
     for (Channel *channel : activeChannels_)
     {
         channel->handleEvent(pollReturnTime_);
     }
     doPendingFunctors();
     ```

     





 

