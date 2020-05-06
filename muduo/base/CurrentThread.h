// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include "muduo/base/Types.h"

namespace muduo
{
namespace CurrentThread //目前程序所在线程的数据，专门分了个命名空间
{
extern __thread int t_cachedTid;
extern __thread char t_tidString[32];
extern __thread int t_tidStringLength;
extern __thread const char *t_threadName;
void cacheTid();

inline int tid()
{
  if (t_cachedTid == 0) //说明线程还没有缓存过数据
  {
    cacheTid(); //通过系统调用缓存得到线程pid
  }
  return t_cachedTid;
}

inline const char *tidString() // for logging
{
  return t_tidString; //tid的字符串表
}

inline int tidStringLength() // for logging
{
  return t_tidStringLength; //字符串表长
}

inline const char *name()
{
  return t_threadName; //线程名
}

bool isMainThread(); //判断是否在主线程

void sleepUsec(int64_t usec); //睡眠
} // namespace CurrentThread
}  // namespace muduo

#endif  // MUDUO_BASE_CURRENTTHREAD_H
