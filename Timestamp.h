#pragma once

#include <iostream>
#include <string>
 
class Timestamp
{
public:
    Timestamp();
    /*
    这里添加explicit的原因是什么 ？ 
        Timestamp 6:56 含有参数的构造函数 使用explicit
    防止出现隐式转换，
    */
    explicit Timestamp(int64_t microSecondsSinceEpoch);
    static Timestamp now();

    // 使用常函数的原因
    std::string toString() const; 

private:
    int64_t microSecondsSinceEpoch_;  
};