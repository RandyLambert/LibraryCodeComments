// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThread.h"

#include "muduo/net/EventLoop.h"

using namespace muduo;
using namespace muduo::net;

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const string &name)
    : loop_(NULL),
      exiting_(false),
      thread_(std::bind(&EventLoopThread::threadFunc, this), name),
      mutex_(),
      cond_(mutex_),
      callback_(cb)
{
}

//main函数实例调用
//EventLoop* loop = loopThread.startLoop();
////异步调用runInThread,即将runInThread添加到loop对象所在的io线程，让该io线程执行
//loop->runInLoop(runInThread);
//sleep(1);
////runAfter内部也调用了runInLoop，所以这里也是异步调用
//loop->runAfter(2,runInThread);
//sleep(3);
//loop->quit();

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
    {
        // still a tiny chance to call destructed object, if threadFunc exits just now.
        // but when EventLoopThread destructs, usually programming is exiting anyway.
        loop_->quit(); //退出io线程，让io线程的loop循环退出，从而退出io线程
        thread_.join();
    }
}

EventLoop *EventLoopThread::startLoop() //启动线程，是这个线程变成io线程
{
    assert(!thread_.started()); //断言这个线程还没有启动
    thread_.start();            //调用启动这个线程，调用那个回调函数threadfuc

    {
        MutexLockGuard lock(mutex_);
        while (loop_ == NULL)
        {
            cond_.wait();
        }
    }

    return loop_;
}

void EventLoopThread::threadFunc() //新创建的线程调用的函数，和上面那个函数调用时间不定，所以要用条件变量控制
{
    EventLoop loop;

    if (callback_)
    {
        callback_(&loop); //初始化操作
    }

    {
        MutexLockGuard lock(mutex_);
        //loop指针指向了一个栈上的对象，threadfunc函数退出之后，指针就失效了
        //threadfunc函数退出，就意味这线程退出了，eventloopthread对象也就没有了存在价值了
        //因而不会有大的问题
        loop_ = &loop;
        cond_.notify();
    }

    loop.loop();
    //assert(exiting_);
    loop_ = NULL;
}
