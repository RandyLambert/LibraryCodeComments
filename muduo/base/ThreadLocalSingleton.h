// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCALSINGLETON_H
#define MUDUO_BASE_THREADLOCALSINGLETON_H

#include "muduo/base/noncopyable.h"

#include <assert.h>
#include <pthread.h>

namespace muduo
{
//运用了__thread和类
template <typename T>
class ThreadLocalSingleton : noncopyable
{
public:
    ThreadLocalSingleton() = delete;
    ~ThreadLocalSingleton() = delete;
    static T &instance() //返回单对象的引用
    {
        if (!t_value_) //如果指针为空创建对象
        {
            t_value_ = new T();
            deleter_.set(t_value_); //这里的set，把刚申请内存的指针设置进去
        }
        return *t_value_;
    }

    static T *pointer() //返回指针
    {
        return t_value_;
    }

private:
    static void destructor(void *obj) //摧毁
    {
        assert(obj == t_value_);
        typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1]; //判断是不完全类型在编译就能发现
        T_must_be_complete_type dummy;
        (void)dummy;
        delete t_value_; //不需要显示调用
        t_value_ = 0;
    }

    class Deleter
    {
    public:
        Deleter()
        {
            pthread_key_create(&pkey_, &ThreadLocalSingleton::destructor); //创建了一个key，析构函数是线程类的释放函数，
            //这个类的作用就是在这里调用回调这个函数,不需要手动销毁
        }

        ~Deleter()
        {
            pthread_key_delete(pkey_);
        }

        void set(T *newObj) //把指针设定进来
        {
            assert(pthread_getspecific(pkey_) == NULL);
            pthread_setspecific(pkey_, newObj); //通过key获取他
        }

        pthread_key_t pkey_;
    };

    static __thread T *t_value_; //每个线程都有这个指针
    static Deleter deleter_;     //Deleter主要为了销毁的对象
};

template <typename T>
__thread T *ThreadLocalSingleton<T>::t_value_ = 0;

template <typename T>
typename ThreadLocalSingleton<T>::Deleter ThreadLocalSingleton<T>::deleter_;

} // namespace muduo
#endif // MUDUO_BASE_THREADLOCALSINGLETON_H
