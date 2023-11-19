#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"
#include <errno.h>
#include <memory>

// handle_error
#include <sys/types.h>
#include <sys/socket.h>
#include <strings.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <string>
/*
*/

static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s: %s: %d TcpConnection Loop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

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


TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd = %d state = %d\n",
                name_.c_str(), channel_->fd(), (int)state_);
}




void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
        // 建立连接的用户有可读事件发生了  调用用户传入的回调操作onmessage
        // shared_from_this 当前对象的指针指针
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

void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if(outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    // 唤醒loop 对应的thread线程，执行回调函数
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this())
                    );
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }else
    {
        LOG_ERROR("TcpConnection fd = %d is down , no more writing\n", channel_->fd());
    }
}
// poller ->  channel::closeCallback() => tcpConnection::handleClose
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

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;   
    }
    else 
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n",name_.c_str(), err);
}


// send 绑定sendInLoop函数处理方法 send 方法重点理解
void TcpConnection:: send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        //在当前的loop是否在对应的线程中
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else 
        {
            loop_->runInLoop(std::bind(
                &TcpConnection::sendInLoop,
                this,
                buf.c_str(),
                buf.size()
            ));
        }
    }
}

// 发送数据， 应用的写速度快， 而内核发送数据慢， 需要把发送的数据写入缓冲区 并且设置了 水位回调函数
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    // 之前调用过connection的shutdown 则不能发送
    if (state_ = kDisconnected)
    {
        LOG_ERROR("disconnected, give up  writing\n");
        return ;
    }
    
    // 表示channel第一次开始写数据， 并且缓冲区没有待发送数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote =  ::write(channel_->fd(), data, len);
        if (nwrote > 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 数据一次性发送完成 就不用给channel设置epollout事件
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this())
                );
            }
        }
        else
        {
            nwrote = 0;
            // wouldblock  非阻塞没有数据此时正常返回
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    faultError = true;
                }
            }
            
        }
    }
    /* 
    说明当前的这一次write， 并没有把数据全部发送出去，剩余的数据需要保存到缓冲区中，
    之后给channel 注册epollout事件， poller发现tcp发送的缓冲区有空间， 
    会通知相应的sock-channel， 调用handle_WriteCallback的回调方法。
    调用tcpconnection：： handleWrite方法，把发送缓冲区的数据全部发送完成
    */
    if (!faultError && remaining > 0)
    {
        // 
        size_t oldLen  = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_
            && oldLen < highWaterMark_
            && highWaterMarkCallback_)
        {
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining)
            );
        }
        outputBuffer_.append((char*)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
            //注册channel的写事件，否则poller不会给channel通知epollout
            channel_->enableWriting();
        }
    }
}


// 建立连接 TcpConnection 管理channel
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
// 连接销毁 关闭 
void TcpConnection::connectDestoryed()
{
    if(state_ == kConnected)
    {
        setState(kDisconnected);
        // 取消监听所有的事件 从poller 中删除
        channel_->disableAll();
    }
    
    // 把channel从poller 中删除掉
    channel_->remove();
}

void TcpConnection::shutdown()
{
    if (state_ == kDisconnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(
            std::bind(&TcpConnection::shutdownInLoop, this)
        );
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting()) // 说明当前outputbuffer 中的数据已经穿发送完成
    {
        socket_->shutdownWrite();
    } 
}