// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_SINGLETON_H
#define MUDUO_BASE_SINGLETON_H

#include "muduo/base/noncopyable.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h> // atexit

namespace muduo
{

namespace detail
{
// This doesn't detect inherited member functions!不能侦测继承的成员函数
// http://stackoverflow.com/questions/1966362/sfinae-to-check-for-inherited-member-functions

/*
SFINAE的意思是这样的，假如有一个特化会导致编译时错误(即出现编译失败)，只要还有别的选择可以被选择，
那么就无视这个特化错误而去选择另外的可选选择。
这个示例中，如果我们给传的参数T类型为POD类型，
当调用detail::has_no_destroy<T>::value时，T参数会在has_no_destroy类中实例化模板，
由于是POD类型，不具备no_destroy方法，不可以使用&C::no_destroy方式调用，意味这如果匹配这个会导致编译错误，
那么它会寻找下一个去实例化。
不过因为test(...)的存在，任何不匹配上一个的到这里都会被接收，所以我们声明出来的成员变量test会是int32_t类型，而不是char类型。
则下一行的value，由于测量test零初始化的字节数，相当于sizeof(int32_t) != 1，所以value的值被确定为false。
我们退出这个函数，回到调用处。
if(!detail::has_no_destroy<T>::value)此时if语句不成立，所以不用注册atexit时的destroy函数。
注意，no_destroy必须是public类型
*/
template <typename T>
struct has_no_destroy //注意，no_destroy必须是public类型
{
  template <typename C>                       //如果我们给传的参数T类型为POD类型，
  static char test(decltype(&C::no_destroy)); //由于是POD类型，不具备no_destroy方法，不可以使用&C::no_destroy方式调用，意味这如果匹配这个会导致编译错误，
  template <typename C>
  static int32_t test(...); //因为test(...)的存在，任何不匹配上一个的到这里都会被接收，所以我们声明出来的成员变量test会是int32_t类型，而不是char类型。
  //则下一行的value，由于测量test零初始化的字节数，相当于sizeof(int32_t) != 1，所以value的值被确定为false。
  const static bool value = sizeof(test<T>(0)) == 1; //判断如果是类的话，是否有no_destroy方法。
};
} // namespace detail

template <typename T>
class Singleton
{
public:
  static T &instance()
  {
    pthread_once(&ponce_, &Singleton::init); //在第一次返回是调用，能够保证这个对象只被调用一次， 并且pthread_once()能保证线程安全，效率高于mutex
    assert(value_ != NULL);
    return *value_;
  }
private:
  Singleton();
  ~Singleton();

  static void init()
  {
    value_ = new T();                      //在init方法中创建
    if (!detail::has_no_destroy<T>::value) //当调用detail::has_no_destroy<T>::value时，T参数会在has_no_destroy类中实例化模板，
    {
      //if(!detail::has_no_destroy<T>::value)此时if语句不成立，所以不用注册atexit时的destroy函数。
      ::atexit(destroy); //程序结束后会自动销毁，在整个程序结束的时候会自动调用这个函数销毁
    }
  }

  static void destroy() //销毁
  {
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1]; //其实这是一种不完全类型检测：T如果不是不完全类型那么sizeof（T）就应该是type_must_be_complete[-1],
    //数组是不能为负数的，所以就会报错; 什么又是不完全类型呢？简单理解就是类型的定义不完整，比如只对类进行了声明，却未定义；

    T_must_be_complete_type dummy;
    (void)dummy;

    delete value_;
    value_ = NULL;
  }

private:
  static pthread_once_t ponce_;
  static T *value_;
};

template <typename T>
pthread_once_t Singleton<T>::ponce_ = PTHREAD_ONCE_INIT;//初始化

template <typename T>
T *Singleton<T>::value_ = NULL;

}  // namespace muduo

#endif  // MUDUO_BASE_SINGLETON_H
