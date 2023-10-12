#include "Buffer.h"
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

ssize_t Buffer::readFd(int fd, int* saveErrno)
{
    // 栈上的内存空间
    char extrabuf[65536] = {0};
    
    // 包含在sys/uio.h
    struct iovec vec[2];
    
    const size_t writable = writableBytes();
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;

    const int iovcnt = (writable < sizeof extrabuf) ?  2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    
    if (n < 0)
    {
        *saveErrno = errno;
    }
    else if (n <= writable) // buffer的
    {
        writerIndex_ += n;
    }
    else
    {
        writerIndex_ = buffer_.size();
        // 这里是从writerIndex开始写数据的
        append(extrabuf, n - writable);
    }
    // 
    return n;
}

ssize_t Buffer::writeFd(int fd, int* saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}