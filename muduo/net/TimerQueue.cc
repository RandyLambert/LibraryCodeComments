// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include "muduo/net/TimerQueue.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Timer.h"
#include "muduo/net/TimerId.h"

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
namespace net
{
namespace detail
{

int createTimerfd()
{
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0)
  {
    LOG_SYSFATAL << "Failed in timerfd_create";
  }
  return timerfd;
}

//计算超时时刻和当前时间的时间差
struct timespec howMuchTimeFromNow(Timestamp when) //将when转换成timespec
{
    int64_t microseconds = when.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();
    if (microseconds < 100) //精确最小到100，小于100当做100
    {
        microseconds = 100;
    }
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>( //秒
                                     microseconds / Timestamp::kMicroSecondsPerSecond);
    ts.tv_nsec = static_cast<long>( //纳秒
                                    (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
    return ts;
}
//清除定时器，避免一直触发
void readTimerfd(int timerfd, Timestamp now)
{
    uint64_t howmany;
    ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
    LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
    if (n != sizeof howmany)
    {
        LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
    }
}
//重置定时器的超时时间
void resetTimerfd(int timerfd, Timestamp expiration)
{
    // wake up loop by timerfd_settime()
    struct itimerspec newValue;
    struct itimerspec oldValue;
    bzero(&newValue, sizeof newValue);
    bzero(&oldValue, sizeof oldValue);
    newValue.it_value = howMuchTimeFromNow(expiration);
    int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
    if (ret)
    {
        LOG_SYSERR << "timerfd_settime()";
    }
}

} // namespace detail
} // namespace net
} // namespace sserver

using namespace sserver;
using namespace sserver::net;
using namespace sserver::net::detail;

TimerQueue::TimerQueue(EventLoop *loop)
    : loop_(loop),
    timerfd_(createTimerfd()),
    timerfdChannel_(loop, timerfd_),
    timers_(),
    callingExpiredTimers_(false)
{
    timerfdChannel_.setReadCallback( //当定时器通道可读时间产生的时候，会回调handleread成员函数
                                     std::bind(&TimerQueue::handleRead, this));
    // we are always reading the timerfd, we disarm it with timerfd_settime.
    timerfdChannel_.enableReading(); //这个通道会加到poller来关注，一旦这个通道的可读时间产生就会回调
}

TimerQueue::~TimerQueue()
{
    timerfdChannel_.disableAll();
    timerfdChannel_.remove();
    ::close(timerfd_);
    // do not remove channel, since we're in EventLoop::dtor();
    for (const Entry &timer : timers_)
    {
        delete timer.second;
    }
}

TimerId TimerQueue::addTimer(TimerCallback cb, //cb定时器的回调函数，可以跨线程调用
                             Timestamp when,   //超时时间
                             double interval)  //间隔时间
{
    Timer *timer = new Timer(std::move(cb), when, interval); //interval如果不为0，说明他是一个重复的定时任务，每过interval就在执行一次，会调cb
    loop_->runInLoop(                                        //跨线程调用实现函数
                                                             std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)
{
    loop_->runInLoop(
                     std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void TimerQueue::addTimerInLoop(Timer *timer) //不会跨线程调用，不需要对临界资源进行保护
{
    loop_->assertInLoopThread();
    //插入一个定时器，有可能会使最早到期的定时器发生改变
    bool earliestChanged = insert(timer); //插入一个定时器可能会比原有的定时器还早，这样earliestchanged会为true

    if (earliestChanged) //true
    {
        //重置定时器的超时时刻(timerfd_settime)
        resetTimerfd(timerfd_, timer->expiration()); //定时器fd和定时时间
    }
}

void TimerQueue::cancelInLoop(TimerId timerId) //不会跨线程调用，不需要对临界资源进行保护
{
    loop_->assertInLoopThread(); //断言在该线程中
    assert(timers_.size() == activeTimers_.size());
    ActiveTimer timer(timerId.timer_, timerId.sequence_);
    //查找该定时器
    ActiveTimerSet::iterator it = activeTimers_.find(timer);
    if (it != activeTimers_.end())
    {
        size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
        assert(n == 1);
        (void)n;
        delete it->first; // FIXME: no delete please，如果用可unique_ptr就不需要手动删除了
        activeTimers_.erase(it);
    }
    else if (callingExpiredTimers_) //如果不在列表中
    {
        //已经到期，并且正在调用回调函数的定时器
        cancelingTimers_.insert(timer); //插入到cancelingTimers，因为有的定时器多次执行，加入到cancelingTimers_中会使虽然过期的他也能被取消
    }
    assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead() //实际上只关注最早的定时器
{
    loop_->assertInLoopThread();
    Timestamp now(Timestamp::now());
    readTimerfd(timerfd_, now); //清除该事件，避免一直触发

    //获取该时刻之前所有的定时器列表(即超时定时器列表)
    std::vector<Entry> expired = getExpired(now); //这个时刻可能好几个定时器超时了，都得处理

    callingExpiredTimers_ = true; //处于处理到期定时器时间
    cancelingTimers_.clear();
    // safe to callback outside critical section

    for (const Entry &it : expired)
    {
        //这里的回调定时器处理函数
        it.second->run();
    }
    callingExpiredTimers_ = false;
    //不是一次性定时器，需要重启
    reset(expired, now);
}
//这里不需要拷贝构造，构造器会进行rvo优化
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    assert(timers_.size() == activeTimers_.size());
    std::vector<Entry> expired;
    Entry sentry(now, reinterpret_cast<Timer *>(UINTPTR_MAX));
    //返回第一个未到期的Timer的迭代器,二分
    TimerList::iterator end = timers_.lower_bound(sentry);
    assert(end == timers_.end() || now < end->first); //断言找到了
    //将到期的定时器插入expired中
    std::copy(timers_.begin(), end, back_inserter(expired));
    //从timers_中移除到期的定时器
    timers_.erase(timers_.begin(), end);

    //从activeTimers_中移除到期的定时器
    for (const Entry &it : expired)
    {
        ActiveTimer timer(it.second, it.second->sequence());
        size_t n = activeTimers_.erase(timer);
        assert(n == 1);
        (void)n;
    }

    assert(timers_.size() == activeTimers_.size());
    return expired; //到期的定时器被返回
}

void TimerQueue::reset(const std::vector<Entry> &expired, Timestamp now)
{ //执行重复执行定时器的函数
    Timestamp nextExpire;

    for (const Entry &it : expired)
    {
        ActiveTimer timer(it.second, it.second->sequence());
        //如果是重复的定时器，并且是未取消的定时器，则重启该定时器
        if (it.second->repeat() && cancelingTimers_.find(timer) == cancelingTimers_.end())
        { //如果没有被取消，而且是重复的定时器，就重启
            it.second->restart(now);
            insert(it.second);
        }
        else
        {
            // 一次性定时器或者已被取消的定时器是不能重置，因此删除该定时器
            // FIXME move to a free list
            delete it.second; // FIXME: no delete please
        }
    }
    //获取最早到期的定时器超时时间
    if (!timers_.empty())
    {
        nextExpire = timers_.begin()->second->expiration();
    }

    if (nextExpire.valid())
    {
        resetTimerfd(timerfd_, nextExpire);
    }
}

bool TimerQueue::insert(Timer *timer)
{
    loop_->assertInLoopThread();
    assert(timers_.size() == activeTimers_.size()); //之前说过timers_和activeTimers_存的东西是一样的
    //最早到器的时间是否改变
    bool earliestChanged = false;
    Timestamp when = timer->expiration();     //timer的到期时间取出来
    TimerList::iterator it = timers_.begin(); //第一个定时器，也就是时间最早的定时器

    //如果timers_为空或者when小于timers_中的最早到期时间
    if (it == timers_.end() || when < it->first)
    {
        earliestChanged = true; //需要更新变量
    }
    {
        //插入到timers_中，按到期时间排序
        std::pair<TimerList::iterator, bool> result = timers_.insert(Entry(when, timer));
        assert(result.second);
        (void)result;
    }
    {
        //插入到activeTimers_中，按定时器对象地址大小排序
        std::pair<ActiveTimerSet::iterator, bool> result = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
        assert(result.second);
        (void)result;
    }

    assert(timers_.size() == activeTimers_.size());
    return earliestChanged; //最早到期时间是否改变
}
