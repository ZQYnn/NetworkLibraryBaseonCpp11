#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"
#include "Timestamp.h"
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>


// 未添加到poller 中
const int kNew = -1; // channel 中的index 初始化 也是 -1

// channel添加到poller 中
const int kAdded = 1;

// channel从 poller 中删除
const int kDeleted = 2;


EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize)

{
    if (epollfd_ < 0) 
    {
        LOG_FATAL("epoll create error %d \n", errno);
    }
}

// 析构函数
EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

/*
EventLoop -> poller-> EPollPoller - > poll   通过返回给EevntLoop中 ChannelList中
*/
Timestamp EPollPoller::poll (int timeoutMs, ChannelList *activeChannels)
{ 
    // 实际上使用log——debug更加合理 
    LOG_INFO("func=%s => fd total count: %lu\n", __FUNCTION__, channels_.size()); 
    
                                            // events_.begin()  返回vector首元素的iterator
                                            //*event_.begin() 首元素 对应的值
                                            // &* 首元素对应的值的地址
    int numsEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    // 如果返回的是
    int saveErrno = errno; 
    
    // 这个并没有完全理解 时间戳类么
    Timestamp now(Timestamp::now());
    
    if (numsEvents > 0)
    {
        LOG_INFO("%d events happened \n",numsEvents);
        
        fillActiveChannels(numsEvents, activeChannels);
        
        if (numsEvents == events_.size())
        {
            // vector  扩容的
            events_.resize(events_.size() * 2);
        }
    }else if (numsEvents == 0)
    {
        LOG_DEBUG("%s timeout !\n", __FUNCTION__); 
    }
    else 
    {
        if (saveErrno != EINTR)
        {
            // 16mins
            errno = saveErrno;
            LOG_ERROR("EPollerPoller::poll() err");
        }
    }
    return now;
}
//
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannel) const
{
    for (int i = 0; i < numEvents; i ++)
    {
        Channel *channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        // EventLoop 获取poller 返回获取的Channel列表
        activeChannel->push_back(channel);
    }
}

/*
channel  中的update方法 最终通过EventLoop调用了 EPollPoller 中的updateChanel方法 
 channel.update ——》 EventLoop  updateChannel -》 poller -》 epollpoller
*/

/*
EventLoop 中包含了ChannelList 和Poller
Poller 中包含了ChannelMap<fd, Channel*>
        EventLoop
    ChannelList     Poller
                    ChannelMap 
*/
void EPollPoller::updateChannel(Channel *channel ) 
{
    // channel 的状态
    const int index =  channel->index();
    LOG_INFO("func=%s fd=%d events=%d idx=%d\n", __FUNCTION__,  channel->fd(), channel->events(), index);
    
    // 
    if (index == kNew || index == kDeleted)
    {
        if (index == kNew)
        {
            int fd = channel->fd();
            channels_[fd] = channel;  
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else
    {
        int fd = channel->fd(); 
        (void)fd;
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            //设置状态为已经删除
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}


// remove方法 同理 update
// 从chanel 中删除， 如果已经添加到epoll监听中， 需要从poller 中删除
void EPollPoller::removeChannel(Channel *channel)
{
    
    LOG_INFO("func=%s fd=%d\n", __FUNCTION__,  channel->fd());

    int fd = channel->fd();
    int index = channel->index();
    
    channels_.erase(fd);
    if (index == kAdded )
    {
        update(EPOLL_CTL_DEL, channel);   
    }
    channel->set_index(kNew);
}



/// @brief   
/// @param operation 
/// @param channel  
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