// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoopThreadPool.h"

#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThread.h"

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const string &nameArg)
    : baseLoop_(baseLoop),
      name_(nameArg),
      started_(false),
      numThreads_(0),
      next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // Don't delete loop, it's stack variable
    //不需要释放loop对象，因为他们都是栈上对象，也不需要释放eventloopThread对象，因为他交给了ptr_vector管理了，当threads_销毁的时候，他们也都销毁了
}

void EventLoopThreadPool::start(const ThreadInitCallback &cb) //启动线程池
{
    assert(!started_);
    baseLoop_->assertInLoopThread();

    started_ = true;

    for (int i = 0; i < numThreads_; ++i)
    {
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
        EventLoopThread *t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        loops_.push_back(t->startLoop()); //启动eventloopThreads线程，在进入事件循环之前，会调用cb
    }
    if (numThreads_ == 0 && cb)
    { //只有一个eventloop，在这个eventloop进入事件循环之前，调用cb
        cb(baseLoop_);
    }
}

EventLoop *EventLoopThreadPool::getNextLoop()
{
    baseLoop_->assertInLoopThread();
    assert(started_);
    EventLoop *loop = baseLoop_;
    //如果loops_为空，则loop指向baseloop_
    //如果不为空，按照round—robin（rr，轮叫）的调度方式选择一个eventloop
    if (!loops_.empty()) //说明现在有不少的线程
    {
        // round-robin
        loop = loops_[next_];
        ++next_;
        if (boost::implicit_cast<size_t>(next_) >= loops_.size())
        {
            next_ = 0;
        }
    }
    return loop;
}

EventLoop *EventLoopThreadPool::getLoopForHash(size_t hashCode)
{
    baseLoop_->assertInLoopThread();
    EventLoop *loop = baseLoop_;

    if (!loops_.empty())
    {
        loop = loops_[hashCode % loops_.size()];
    }
    return loop;
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    baseLoop_->assertInLoopThread();
    assert(started_);
    if (loops_.empty())
    {
        return std::vector<EventLoop *>(1, baseLoop_);
    }
    else
    {
        return loops_;
    }
}
