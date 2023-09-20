#pragma once

/*
noncopyable 被继承以后， 派生类对象可以正常构造和析构
            但是无法进行拷贝构造和赋值操作， 这样使用

*/
class noncopyable
{

public:
    noncopyable(const noncopyable &) = delete;
    noncopyable& operator=(const noncopyable &) = delete;
protected: 
    noncopyable() = default;
    ~noncopyable() = default;
};