// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/CountDownLatch.h"

using namespace muduo;

CountDownLatch::CountDownLatch(int count)
    : mutex_(),           //构造一个锁，初始化顺序要和成员声明一致
      condition_(mutex_), //condition_不负责生成期，只是一个引用
      count_(count)       //传入一个计数器，初始化
{
}

void CountDownLatch::wait()
{
  MutexLockGuard lock(mutex_); //条件变量的一个使用
  while (count_ > 0)
  {
    condition_.wait();
  }
}

void CountDownLatch::countDown()
{
  MutexLockGuard lock(mutex_);
  --count_;
  if (count_ == 0)
  {
    condition_.notifyAll(); //通知所有等待线程
  }
}

int CountDownLatch::getCount() const //const成员函数，本该不能改变数据成员的状态，但是这个改变了，是因为锁变量的是可变的
{
  MutexLockGuard lock(mutex_); //可能多个线程都要访问这个变量，保护一下
  return count_;
}

