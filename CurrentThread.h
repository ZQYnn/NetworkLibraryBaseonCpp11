// 获取线程

#pragma once

#include <unistd.h>
#include <sys/syscall.h> 


namespace CurrentThread
{
    extern __thread int  t_cacahedTid;
    
    void cacheTid();

    inline int tid()
    {
        if (__builtin_expect(t_cacahedTid == 0, 0))
        {
            cacheTid();
        }
        return t_cacahedTid;
        
    }
    
} // namespace CurrentThread

