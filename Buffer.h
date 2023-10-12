#pragma once

#include <vector>
#include <string>
#include <algorithm>


//网络库底层的缓冲器类型的定义 buffer 设计的问题
// 网络库设计的
/** 
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
cc
*/
class Buffer
{
public:
    static const size_t kCheapPrepend = 0;
    static const size_t kInitialSize = 1024;
    
    explicit Buffer(size_t initialSize = kInitialSize)
                : buffer_(kCheapPrepend + initialSize)
                , readerIndex_(kCheapPrepend)
                , writerIndex_(kCheapPrepend)
    {}
    
    
    size_t readableBytes() const
    {
        return writerIndex_ - readerIndex_;
    }
    
    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;   
    }
    
    size_t prependableBytes() const
    {
        return readerIndex_;
    }
    
    // 返回缓冲区中可读可读数据的起始地址
    const char* peek() const
    {
        return begin() + readerIndex_;
    }
    
    // onMessage buffer -> string
    // 
    void retrieve(size_t len)
    {
        if(len < readableBytes())
        {
            readerIndex_ += len;
        }
        else
        {
            retrieveAll();
        }
    }
    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }
    // 把onMessage 函数上报的buffer数据， 转化成string类型的数据返回
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());
    }
    
    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }


    // 把[data, data + len] 内存上的数据添加到writeable 中
    void append(const char *data, size_t len)
    {
        ensureWriteableBytes(len); 
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }
    
    char *beginWrite()
    {
        return begin() + writerIndex_;
    }
    
    const char* beginWrite() const
    {
        return begin() + writerIndex_;
    }

    void ensureWriteableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len);
        }
    }

    // 有符号整型ssize_t 从fd上读取数据 从fd上读取数据
    ssize_t readFd(int fd, int* saveErrno);

    // 通过fd发送数据 
    ssize_t writeFd(int fd, int* saveErrno);
    
private:
    char* begin()
    { 
        // * 是对buffer.begin() iterator的解引用 获取首元素的值
        // 之后 &的操作是 获取首元素的地址
        return &*buffer_.begin();
    }
    
    const char* begin() const
    {
        return &*buffer_.begin();
    }
    
    void makeSpace(size_t len)
    {
        if (writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            buffer_.resize(writerIndex_ + len); 
        }
        else
        {
            size_t readable  = readableBytes();
            // algorithm  copy： 起始位置iterator  末尾位置iterator ， 被复制的起始位置
            std::copy(begin() + readerIndex_,
                    begin() + writerIndex_,
                    begin() + kCheapPrepend);
                    
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ +readable;
        }
    }
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};