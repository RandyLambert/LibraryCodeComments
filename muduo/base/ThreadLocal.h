// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCAL_H
#define MUDUO_BASE_THREADLOCAL_H

#include "muduo/base/Mutex.h"
#include "muduo/base/noncopyable.h"

#include <pthread.h>

namespace muduo
{

template <typename T> //以模板的形式封装，可以把非pod类型数据放进来
class ThreadLocal : noncopyable
{ //pod类型已经解决，这个是非pod类型
public:
    ThreadLocal()
    {
        MCHECK(pthread_key_create(&pkey_, &ThreadLocal::destructor)); //在调用这个函数时，加入一个回调函数，那个回调函数负责销毁数据，pkey_申请了所有线程都会有这些
    }

    ~ThreadLocal()
    {
        MCHECK(pthread_key_delete(pkey_)); //只是销毁key不是销毁数据，数据用delete销毁的
    }

    T &value()
    {
        T *perThreadValue = static_cast<T *>(pthread_getspecific(pkey_)); //获取线程特定数据
        if (!perThreadValue)                                              //如果返回的指针是空的，说明还么创建，就创建
        {
            T *newObj = new T();                        //新建数据T类
            MCHECK(pthread_setspecific(pkey_, newObj)); //创建
            perThreadValue = newObj;                    //将新类传给成员变量
        }
        return *perThreadValue; //返回
    }

private:
    static void destructor(void *x) //调用这个回调函数，会去销毁数据，在调用pthread_key_create时
    {
        T *obj = static_cast<T *>(x);
        typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1]; //检测一下一定要是完全类型，在编译期就能发现问题
        T_must_be_complete_type dummy;
        (void)dummy; //防止报错
        delete obj;  //销毁
    }

private:
    pthread_key_t pkey_; //每个线程都有这个，但是他可以指定特定数据，所以每个线程可以指定不同的数据。
};

} // namespace muduo

#endif // MUDUO_BASE_THREADLOCAL_H
