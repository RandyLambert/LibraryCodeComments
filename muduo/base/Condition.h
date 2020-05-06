// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CONDITION_H
#define MUDUO_BASE_CONDITION_H

#include "muduo/base/Mutex.h"

#include <pthread.h>

namespace muduo
{

class Condition : noncopyable
{
public:
    explicit Condition(MutexLock &mutex)
        : mutex_(mutex)
    {
        MCHECK(pthread_cond_init(&pcond_, NULL)); //初始化
    }

    ~Condition()
    {
        MCHECK(pthread_cond_destroy(&pcond_));
    }

    void wait()
    {
        //https://www.cnblogs.com/lxy-xf/p/11289078.html
        MutexLock::UnassignGuard ug(mutex_);
        //条件变量，一般使用条件变量时，要配合while()来防止虚假唤醒
        pthread_cond_wait(&pcond_, mutex_.getPthreadMutex()); //阻塞
    }

    // returns true if time out, false otherwise.
    bool waitForSeconds(int seconds);

    void notify()
    {
        MCHECK(pthread_cond_signal(&pcond_)); //唤醒一个
    }

    void notifyAll()
    {
        MCHECK(pthread_cond_broadcast(&pcond_)); //唤醒所有
    }

private:
    MutexLock &mutex_;     //只是引用使用锁，两个类只是关联关系
    pthread_cond_t pcond_; //条件变量
};
}; // namespace muduo

} // namespace muduo

#endif // MUDUO_BASE_CONDITION_H
