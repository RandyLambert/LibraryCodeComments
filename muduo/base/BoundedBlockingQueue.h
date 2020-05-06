// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H
#define MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

#include <boost/circular_buffer.hpp>
#include <assert.h>

namespace muduo
{

template <typename T>
class BoundedBlockingQueue : noncopyable
{
public:
    explicit BoundedBlockingQueue(int maxSize)
        : mutex_(),
          notEmpty_(mutex_),
          notFull_(mutex_),
          queue_(maxSize) //队列的大小
    {
    }
    BoundedBlockingQueue(const BoundedBlockingQueue &) = delete; //禁止拷贝构造
    BoundedBlockingQueue &operator=(const BoundedBlockingQueue &) = delete;

    void put(const T &x)
    {
        MutexLockGuard lock(mutex_); //生产产品首先得判断队列是否满了
        while (queue_.full())        //如果对列满了的，循环判断
        {
            notFull_.wait();
        }
        assert(!queue_.full()); //现在结束了，断言队列不为满
        queue_.push_back(x);    //生产产品
        notEmpty_.notify();     //因为生产了产品说明队列不为空，唤醒消费者
    }

    T take()
    {
        MutexLockGuard lock(mutex_); //消费者，保护对列
        while (queue_.empty())       //判断队列是否为空，在这里阻塞
        {
            notEmpty_.wait();
        }
        assert(!queue_.empty());            //断言队列不为空
        T front(std::move(queue_.front())); //一旦消费了产品，说明现在对列不满
        queue_.pop_front();
        notFull_.notify(); //条件变量通知
        return front;
    }

    bool empty() const
    {
        MutexLockGuard lock(mutex_);
        return queue_.empty();
    }

    bool full() const
    {
        MutexLockGuard lock(mutex_);
        return queue_.full();
    }

    size_t size() const
    {
        MutexLockGuard lock(mutex_);
        return queue_.size();
    }

    size_t capacity() const
    {
        MutexLockGuard lock(mutex_);
        return queue_.capacity();
    }

private:
    mutable MutexLock mutex_; //锁，在const中也需要使用锁保护
    Condition notEmpty_ GUARDED_BY(mutex_);
    Condition notFull_ GUARDED_BY(mutex_);
    boost::circular_buffer<T> queue_ GUARDED_BY(mutex_); //boost中的环形缓冲区
};

} // namespace muduo

#endif // MUDUO_BASE_BOUNDEDBLOCKINGQUEUE_H
