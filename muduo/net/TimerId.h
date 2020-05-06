// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TIMERID_H
#define MUDUO_NET_TIMERID_H

#include "muduo/base/copyable.h"

namespace muduo
{
namespace net
{

class Timer;

///
//An opaque identifier, for canceling Timer.
//是一个不透明的定时器id，用来取消定时器
//不透明的意思是我们需要关注他的存在
//透明的意思是我们不需要关注他的存在，当他是空气
class TimerId : public muduo::copyable//是一个外部可见的一个类，只有两个数据成员
{
 public:
  TimerId()
    : timer_(NULL),
      sequence_(0)
  {
  }

  TimerId(Timer* timer, int64_t seq)
    : timer_(timer),
      sequence_(seq)
  {
  }

  // default copy-ctor, dtor and assignment are okay

  friend class TimerQueue;

 private:
    Timer *timer_;     //定时器的地址
    int64_t sequence_; //定时器的序号，timer类中也有
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TIMERID_H
