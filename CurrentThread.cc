#include "CurrentThread.h"


namespace CurrentThread
{
    __thread int t_cachedTid = 0;
    
    void cacheTid()
    {
        if (t_cacahedTid == 0)
        {
            t_cacahedTid = static_cast<pid_t>(::syscall(SYS_gettid));
        }
    }
}
// 