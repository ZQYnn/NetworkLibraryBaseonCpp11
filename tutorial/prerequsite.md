



# 函数对象与绑定器

## 函数指针

在设计与实现muduo过程中，大量使用了函数对象，即函数式编程，函数式编程思想源于C语言中的函数指针，了解函数式编程之前，先来回顾C语言中函数指针。

**函数指针**：对于常用变量有`int *a`这种写法，即一个指向`int`类型的指针变量a，那么对于指针函数的理解为：**一个指向函数的指针**

```c
int (*p)(int , int); 
// 含义: p是一个指向函数返回值为int并且传递参数列表为两个int类型的指针p
```

注意：函数指针的类型需要与函数类型一致， 即函数参数列表以及返回值

```c
void func1()
{
  printf("hello");
}
void (*p1)() = func1;


int func2(int a, int b)
{
  return a + b;
}
int (*p2)(int, int) = func2;
```

**使用函数指针调用函数**

```c
#include <stdio.h>
int add(int a, int b)
{
    return a + b;
}
int main()
{
    int (*func)(int, int) = add;
    printf("%d\n", func(1, 2));
    return 0;
}
```

**函数指针本身作为参数列表**：

```c
#include <stdio.h>
int add(int a, int b)
{
    return a + b;
}
int sub(int a, int b)
{
    return a - b;
}

void calculate(int x, int y, int (*f)(int a, int b))
{
    printf("%d\n", f(x, y));
}
int main() {
    calculate(10, 20, add); // 30
    calculate(10, 20, sub); // -10;
    return 0;
}
```

**重点**：以上函数调用方式应用广泛，有点在于可扩展型强以及编写简单省去OOP特性

## 函数指针优点

**扩展性强、低耦合**

如果在计算的方法中需要添加一个乘法功能时， 只需要在原来基础上单独实现一个乘法函数即可，保证不同模块之间相互独立。

```c
#include <stdio.h>
int add(int a, int b)
{
    return a + b;
}
int sub(int a, int b)
{
    return a - b;
}
int mul(int a, int b)
{
  return a * b;
}
void calculate(int x, int y, int (*f)(int a, int b))
{
    printf("%d\n", f(x, y));
}
int main() {
    calculate(10, 20, add); // 30
    calculate(10, 20, sub); // -10;
    return 0;
}
```

**省去OOP中动态多态的复杂编写**

仔细观察当前例子， 是否和面向对象中应用到的动态多态案例十分相似？使用C++中OOP特性实现当前案例

```c++
class Calculator
{
public:
    virtual int calculate(int a, int b) = 0;
};

class Add : public Calculator
{
public:
    virtual int calculate(int a, int b)
    {
        return a + b;
    }
};

class sub : public Calculator
{
public:
    virtual int calculate(int a, int b)
    {
        return a - b;
    }
};

void getAns( int x, int y, Calculator *p)
{
    std::cout << p->calculate(x, y) << std::endl;
}

int main()
{
    // 动态多态：基类指针（或引用）指向派生类对象，
  	// 原理：覆盖基类虚函数表中的同名方法
    getAns(10, 20, new Add);
    getAns(10, 20, new sub);
    return 0;
}
```

本质上，这两种调用思想是一致的， 但是使用通过函数指针调用省去了OOP特征中繁杂编写，提高效率。



## 函数对象

**C++中使用函数指针思想**

```c++
template<typename T>
bool greater_(T a, T b)
{
    return  a > b;
}

template<typename T>
bool less_(T a, T b)
{
    return a < b;
}

template<typename T, typename Compare>
bool compare(T a, T b, Compare cmp)
{
    return cmp(a, b);
}

int main()
{
     // 这里的greater 就是函数指针
    std::cout << compare(10, 20, greater_<int>) << std::endl; // 0
}
```





引入**函数对象**：函数对象 属于C++特性，即有`operator()`重载函数的对象或者称为仿函数。

```c++
class Sum
{
public:
    int operator()(int a, int b)
    {
        return a + b;
    }
};
int main()
{
    Sum sum;
    std::cout << sum(10, 20);
}
```

函数对象思想： 通过函数指针间接调用函数 ， 在上述compare的案例中，存在函数指针不能使用inline，使用函数对象效率高， 函数对象是用类生成的， 可以添加函数对象更多信息， 相关成员变量。

```c++
template<typename T>
class greater_
{
public:
    bool operator() (T a, T b)  //二元函数对象
    {
        return a > b;
    }
};

template<typename T>
class less_
{
public:
    bool operator() (T a, T b)
    {
        return a < b;
    }
};

template<typename T, typename Compare>
bool compare(T a, T b, Compare comp)
{

    // 通过函数对象调用 operator() 可以省去调用开销，相对于函数指针（不能inline） 效率高
    return comp(a, b);
}

int main()
{
    cout << compare(10, 20, greater_<int>()) << endl;
}
```



## C++11 中的functional

在C++11中 引入了functional机制，functional底层依赖**函数对象**机制，functional底层应用类模版偏特化以及可变参数，并将参数传递到`operator()` 重载函数中，这里重点掌握应用方式。

```c++
#include <iostream>
#include <functional>

using namespace std;

void print1()
{
    cout << "hello world" << endl;
}
void print2(string str)
{
    cout << str << endl;
}
int sum(int a, int b)
{
    return a + b;
}
class Test
{
public:
    void hello(string str)  {cout << str << endl; }
};
int main()
{
    // 从function 的类模版的定义， 用函数类型实例化function
    function<void()> func1 = print1;
    // 另一种调用构造函数的方法 function<void()> func1(print1);

    func1(); // func1.operator()


    function<void(string)> func2 = print2;
    // func2.operator() (string str) => hello(str)
    func2("hello");


    function<int(int, int)> func3 = sum;
    cout << func3(1, 2) << endl;

    // 函数对象 本质 就是重载了 operator()
    function<int(int, int)> func4 = [](int a, int b) -> int {return a + b; };
    cout << func4(1, 200) << endl;

    // 调用成员函数的存在隐藏的参数 类对象本身， this
    function<void(Test*, string)> func5 = &Test::hello;
   
    func5(new Test(), "Test::hello");
    return 0;
}
```



## bind 绑定器

c++ 11 绑定器 返回的还是 函数对象  绑定函数对象。

在之前的Function中内容，使用function 底层原理就是使用类模版实现 `operator()`符号重载, 结合函数指针的

- 调用方式： 匿名函数对象绑定， bind返回的是函数对象类型的。

     ```c++
     #include <functional>
     int sum(int a, int b) { return a + b;}
     
     int main()
     {
       // 绑定函数 对象并传参数
       bind(sum, 10, 29);
     	
       // 进行函数绑定并且执行  bind(sum, 10, 20) 相当于绑定了 匿名函数对象 
       // 在bind(sum, 10, 20)之后 加上() 就是匿名函数对象 
       // 这里() 中不添加参数  是因为在之前的bind都已经绑定进去， 相当于写死了传入的参数
        bind(sum, 10, 29)(); // 这里就是相当于 sum(10, 29);
       return 0;
     }
     ```

- 使用`<functional>` 函数接收函数对象,  如果是传递参数不需要手动设置时（在bind 函数中 设置好了参数 ），设置接受参数列表为空。

     ```c++
     int sum(int a, int b) {return a + b;}
     int main()
     {
         function<int()> func = bind(sum, 10, 20);
         cout << func() << endl;
         return 0;
     }
     ```

- 接受函数对象， 手动设置参数列表, 使用`placeholers`,    `placeholders::_1` 对应回调函数从左向右的参数列表顺序

     ```c++
     int sum(int a, int b) { return a + b;}
     void print(int a, string str)
     {
         cout << a << ":" << str << endl;
     }
     int main()
     {
         function<int(int, int)> func = bind(sum,  placeholders::_1, placeholders::_2);
         cout << func(20, 50) << endl;
         
         function<void(string, int)> func_ = bind(print, placeholders::_2, placeholders::_1);
         func_("hello", 1);
         return 0;
     }
     ```

- 接受类中的成员方法,接受成员方法的时候， bind参数依次是， 类中成员函数地址， 类对象， 参数。这种方式是muduo中使用到。重点

     ```c++
     class Test
     {
     public:
         int sum(int a, int b) {return a + b;}
     };
     int main()
     {
         function<int(int, int)> func = bind(&Test::sum, new Test(), placeholders::_1, placeholders::_2);
          cout << func(20, 20) << endl;
         return 0;
     }
     ```

- 接受匿名对象lambda表达式

     ```c++
     int main()
     {
         function<int(int, int)> func = [](int a, int b) ->int {return a + b;};
         cout << func(20, 10);
         return 0;
     }
     ```







