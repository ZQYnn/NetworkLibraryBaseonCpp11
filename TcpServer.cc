#include "TcpServer.h"
#include "Logger.h"


EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop; 
}