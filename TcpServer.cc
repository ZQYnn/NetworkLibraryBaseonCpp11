#include "TcpServer.h"
#include "Logger.h"
#include "Acceptor.h"
#include "TcpConnection.h"

#include <strings.h>
#include <functional>

EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop; 
}

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
TcpServer::~TcpServer()
{
    // 这里使用智能指针的操作还是没有理解
    for (auto& item : conncetions_)
    {
        //局部的
        TcpConnectionPtr conn(item.second); 
        item.second.reset();
    
        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDestoryed, conn)
        );   
    }
}


void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

// run in loop 
void TcpServer::start()
{
    if (started_++ == 0)
    {
        // 启动底层的loop线程池 
        threadPool_->start(threadInitCallback_); 
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}

//  acceptor 有新连接的时候调用newConnection, acceptor 
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
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    
    // 设置如何关闭回调  用户调用shutdown
    // conn(tcpConnection) -> shutdown ->shutdownInLoop -> shutWrite -> 
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)
    );
    
    // 直接调用TcpConnection::connectEstablish
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
        
}


void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn)
    );
}


void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop[%s] - connection %s\n",
            name_.c_str(), conn->name().c_str());
            
    conncetions_.erase(conn->name());
    
    EventLoop *ioLoop = conn->getLoop();
    // 这里再次绕到了tcpConnection::connectDestoryed 方法 中执行channel -》remove
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestoryed, conn)
    );
      
}