// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREAD_H
#define MUDUO_BASE_THREAD_H

#include "muduo/base/Atomic.h"
#include "muduo/base/CountDownLatch.h"
#include "muduo/base/Types.h"

#include <functional>
#include <memory>
#include <pthread.h>

namespace muduo
{

class Thread : noncopyable
{
 public:
  typedef std::function<void ()> ThreadFunc;  //基于对象编程，函数接口的定义

  explicit Thread(ThreadFunc, const string& name = string());
  // FIXME: make it movable in C++11
  ~Thread();

  void start(); //开始初始化线程
    int join();   // return pthread_join()

    bool started() const { return started_; } //是否打开
                                              // pthread_t pthreadId() const { return pthreadId_; }
    pid_t tid() const { return tid_; }
    const string &name() const { return name_; }

    static int numCreated() { return numCreated_.get(); } //静态函数只能使用静态变量

private:
    void setDefaultName();

    bool started_;        //线程是否打开
    bool joined_;         //是否join
    pthread_t pthreadId_; //线程id,和下面tid不同
    pid_t tid_;           //线程tid_
    ThreadFunc func_;     //函数接口
    string name_;         //线程名
    CountDownLatch latch_;
    static AtomicInt32 numCreated_; //使用了原子操作，静态变量，整个进程中有多少线程
};

}  // namespace muduo
#endif  // MUDUO_BASE_THREAD_H
