// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_BLOCKINGQUEUE_H
#define MUDUO_BASE_BLOCKINGQUEUE_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

#include <deque>
#include <assert.h>

namespace muduo
{

template <typename T>
class BlockingQueue : noncopyable
{
    BlockingQueue()
        : mutex_(),
          notEmpty_(mutex_),
          queue_()
    {
    }

    void put(const T &x)
    {
        {
            MutexLockGuard lock(mutex_);
            queue_.push_back(x);
        }                   //可加可不加，唤醒操作可以在锁之外
        notEmpty_.notify(); //唤醒可以不加锁控制，所以使用时加一层括号也可在，使他在作用域之内
    }

    void put(T &&x)
    {
        {                                   //同上
            MutexLockGuard lock(mutex_);    //获取锁
            queue_.push_back(std::move(x)); //给对列生产产品
        }
        notEmpty_.notify();
    }

    T take()
    {
        MutexLockGuard lock(mutex_); //保护对列
        // always use a while-loop, due to spurious wakeup
        while (queue_.empty()) //判断队列是否为空
        {
            notEmpty_.wait();
        }
        assert(!queue_.empty()); //断言不为空
        T front(std::move(queue_.front()));
        queue_.pop_front();
        return std::move(front);
    }

    size_t size() const
    {
        MutexLockGuard lock(mutex_);
        return queue_.size();
    }

private:
    mutable MutexLock mutex_; //可变的，因为很多const成员函数也需要用锁
    Condition notEmpty_ GUARDED_BY(mutex_);
    std::deque<T> queue_ GUARDED_BY(mutex_);
};

} // namespace muduo

#endif // MUDUO_BASE_BLOCKINGQUEUE_H
