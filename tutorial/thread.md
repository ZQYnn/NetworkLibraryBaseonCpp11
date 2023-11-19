# Thread

本项目实现多线程主要涉及c++11 thread类以下重点

- `thread`/ `mutex` / `condition_variable`
- `lock_guard`/ `unique_lock`
- `atomic`  原子类型

本项目中使用C++11 提供的thread类， 实例化`thread`类,  传入实例化对象所需要的线程函数以及参数， 自动开启线程。



## 启动线程

```c++
#include <thread>

void threadHandler(int time)
{
    std::this_thread::sleep_for(std::chrono::seconds(time));
    std::cout << "sub thread" << std::endl;
}
int main()
{
    std::thread t1(threadHandler, 2);
  	// 主线程执行到此阻塞， 等子线程运行结束之后再运行。
    t1.join();
    std::cout << "main thread" << std::endl;
    return 0;
}
```



## 线程池案例

结合thread和函数绑定器实现线程池

```c++
#include <functional>
#include <thread>
using namespace  std;
class Thread
{
public:
    Thread(function<void()> func): _func(func) {}
    thread start()
    {
        thread t(_func);
        return t;
    }
private:
    std::function<void()> _func;
};

class ThreadPool
{
public:
    void startPool(int size)
    {
        for (int i = 0; i < size; i ++)
        {
            _pool.push_back(
                    new Thread(bind(&ThreadPool::runInThread, this, i))
            );
        }
        for (int i = 0; i < size; i ++)
        {
            _handler.push_back(_pool[i]->start());
        }

        for (auto &item : _handler)
        {
            item.join();
        }
    }
private:
    vector<Thread*> _pool;
    vector<thread> _handler;
    void runInThread(int idx)
    {
      
        cout << "call runInThread id :" << idx << endl;
    }
};

int main()
{
    ThreadPool pool;
    pool.startPool(20);
    return 0;
}
```



## 线程安全问题

在上述启动多线程的案例的时候，会出现线程安全问题，在`runInThread(int idx)` 函数中， 执行的打印语句是线程不安全的，因为输出语句并非原子操作， 当时间片轮转到当前线程，还没有完成输出语句的时候，时间片到，当前进入线程阻塞状态，导致输出结果错乱。

针对此问题，可以使用线程互斥锁保证线程安全。

```c++
void runInThread(int idx)
{
  mtx.lock();
  cout << "call runInThread id :" << idx << endl;
  mtx.unlock();
}
```



对于临界资源，也可以使用`lock_guard`



```c++
#include <thread>
#include <list>
#include <mutex>
using namespace std;
int cnt = 100;
std::mutex mtx; 

void subFunc(int idx)
{
    while(cnt > 0)
    {
        {
            // lock 出当前作用域析构 所以单独写在这个括号里 是line 的局部对象
            // 栈上的局部对象 在if中退出也会自动析构
            lock_guard<std::mutex> lock(mtx); // scoped_ptr
            if (cnt > 0) {
                ::printf("%d窗口卖出第%d票\n", idx, cnt);
                cnt--;  
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
int main()
{
    list<std::thread> list;
    for (int i = 1; i <= 3; i ++)
    {
        list.push_back(std::thread(subFunc, i));
    }
    for (auto  &t : list) t.join();
    return 0;
}
```









一般地， 保证线程安全可以有使用以下几种方法

- 互斥锁
- 读写锁
- 信号量
- 条件变量