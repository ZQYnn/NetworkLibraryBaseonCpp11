#  相关概念解释

## 项目概述

本项目实现一个基于非阻塞 IO、事件驱动以及 Reactor 设计模式的 C++ 高并发 TCP 网络库，本项 过 one loop per thread 线程模型实现高并发。

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

**阻塞 I/O**，当用户程序执行 `read` ，线程会被阻塞，一直等到内核数据准备好，并把数据从内核缓冲区拷贝到应用程序的缓冲区中，当拷贝过程完成，`read` 才会返回。

注意，**阻塞等待的是「内核数据准备好」和「数据从内核态拷贝到用户态」这两个过程**。过程如下图：

<img src="https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/blocking.png" width=60%>

**非阻塞 I/O**，非阻塞的 read 请求在数据未准备好的情况下立即返回，可以继续往下执行，此时应用程序不断轮询内核，直到数据准备好，内核将数据拷贝到应用程序缓冲区，`read` 调用才可以获取到结果。过程如下图：

<img src="https://pic-go-oss.oss-cn-beijing.aliyuncs.com/muduo/non_blocking.png" width=60%>

`Reactor[one loop per thread: non-blocking + IO multiplexing]`模型。muduo采用的是Reactors in thread有一个main Reactor负责accept(2)连接，然后把连接挂在某个sub Reactor中(muduo中采用的是round-robin的方式来选择sub Reactor)，这样该连接的所有操作都在那个sub Reactor所处的线程中完成。多个连接可能被分到多个线程中，以充分利用CPU。

## 使用eventFd的作用是什么

- eventfd是linux的一个系统调用，为事件通知创建文件描述符，eventfd()创建一个“eventfd对象”，这个对象能被用户空间应用用作一个**事件等待/响应机制**，靠内核去响应用户空间应用事。
- 实现唤醒，让IO线程从IO multiplexing阻塞调用中返回，更高效地唤醒。
