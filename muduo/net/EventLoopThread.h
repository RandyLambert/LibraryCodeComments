// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"
namespace muduo
{
namespace net
{

class EventLoop;

class EventLoopThread : noncopyable
{
public:
    typedef std::function<void(EventLoop *)> ThreadInitCallback;

    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
                    const string &name = string()); //可以传递一个回调函数
    ~EventLoopThread();
    EventLoop *startLoop(); //启动线程，在这个线程里创建一个eventloop对象，该线程成为io线程

private:
    void threadFunc(); //线程函数

    EventLoop *loop_; //loop_指针指向一个eventloop对象
    bool exiting_;    //是否退出
    Thread thread_;
    MutexLock mutex_;
    Condition cond_;
    ThreadInitCallback callback_; //会带哦函数在eventloop::loop时间循环之前被调用初始话
};

} // namespace net
} // namespace muduo

#endif // MUDUO_NET_EVENTLOOPTHREAD_H
