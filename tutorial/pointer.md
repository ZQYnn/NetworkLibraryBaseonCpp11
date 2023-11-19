# 智能指针

智能指针本质上也是通过类模版实现的， 保证资源释放，原理是通过类实现智能指针，在出当前作用域的时候，类析构时自动释放资源， 防止了内存泄漏等情况发生。

## scoped_ptr/unique_ptr

`socped_ptr`/ `unique_ptr` ,   通过



- unique_ptr

     ```c++
     unique_ptr;
     unique_ptr(const unique_ptr<T> &) = delete;
     unique_ptr<T> & operator=(const scoped_ptr<T>&) = delete;
     ```

- scoped_ptr

     ```c++
     scoped_ptr(const scoped_ptr<T>&) = delete;
     scoped_ptr<T> & operator=(const scoped_ptr<T>&) = delete;
     ```

     

## shared_ptr/weak_ptr

是带有引用计数的只能指针， shared_ptr 强智能指针， weak_ptr 弱智能指针

## 智能指针的循环引用问题

```c++
#include <iostream>
#include <memory>

using  namespace  std;
class B;
class A
{
public:
    A() { cout << "A()" << endl; }
    ~A() { cout << "~A()" << endl; }
    shared_ptr<B> _ptrb;
};

class B
{
public:
    B() { cout << "B() " << endl; }
    ~B() { cout << " ~B()" << endl; }
    shared_ptr<A> _ptra;
};


int  main()
{
    shared_ptr<A> pa(new A());
    shared_ptr<B> pb(new B());


    pa->_ptrb = pb;
    pb->_ptra = pa;

    cout << pa.use_count() << endl;
    cout << pb.use_count() << endl;

    return 0;
}
```



## 智能指针解决循环引用



**定义对象的时候使用强智能指针， 引用对象的是否使用弱智能指针** 



