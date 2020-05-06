// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
//也相当一个门栓类
//既可以用于所有的子线程等待主线程发起起跑
//也可以用与主线程等待子线程初始化完毕才开始工作
namespace muduo
{

class CountDownLatch : noncopyable
{
public:
    explicit CountDownLatch(int count);

    void wait();

    void countDown();

    int getCount() const;

private:
    mutable MutexLock mutex_ GUARDED_BY(mutex_); //可变的，所以可以在const函数中改变状态
    Condition condition_ GUARDED_BY(mutex_);     //顺序很重要，先mutex后condition
    int count_;
};

} // namespace muduo
#endif // MUDUO_BASE_COUNTDOWNLATCH_H
