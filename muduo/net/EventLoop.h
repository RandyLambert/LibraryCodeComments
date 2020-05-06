// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOP_H
#define MUDUO_NET_EVENTLOOP_H

#include <atomic>
#include <functional>
#include <vector>

#include <boost/any.hpp>

#include "muduo/base/Mutex.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/TimerId.h"

namespace muduo
{
namespace net
{

class Channel;
class Poller;
class TimerQueue;
/*
one loop per thread的意思是说，每个线程都最多有一个eventloop对象
eventloop对象在构造的时候会检查当前线程是否已经创建了其他eventloop对象
如果已经创建，则终止程序（LOG_FATAL）
EventLoop构造函数会记住本对象的所属线程(threadld_)
创建了eventloop对象称为io线程，其功能是运行时间循环（eventloop::loop）
*/

///
/// Reactor, at most one per thread.
///
/// This is an interface class, so don't expose too much details.

///由于每个套接字和一个Channel相关联。所以EventLoop只需要管理所有需要关注的套接字相关的Channel即可，
///所以这里有一个ChannelList,EventLoop只需要关注有事件的套接字，在Poller_返回后将有事件
///发生的套接字作为一个集合，activeChannels_就是被激活的套机字所在的Channel组成的结合
///
class EventLoop : noncopyable //rector模式的封装
{
public:
    typedef std::function<void()> Functor;

    EventLoop();
    ~EventLoop(); // force out-line dtor, for scoped_ptr members.

    ///
    /// Loops forever.
    ///
    /// Must be called in the same thread as creation of the object.
    /// 主循环
    void loop();

    /// Quits loop.
    /// 退出主循环
    /// This is not 100% thread safe, if you call through a raw pointer,
    /// better to call through shared_ptr<EventLoop> for 100% safety.
    void quit();

    ///
    /// Time when poll returns, usually means data arrival.
    ///poll延迟的时间
    Timestamp pollReturnTime() const { return pollReturnTime_; }

    /// 迭代次数
    int64_t iteration() const { return iteration_; }

    /// Runs callback immediately in the loop thread.
    /// It wakes up the loop, and run the cb.
    /// If in the same loop thread, cb is run within the function.
    /// Safe to call from other threads.
    /// 在主循环中运行
    void runInLoop(Functor cb);
    /// Queues callback in the loop thread.
    /// Runs after finish pooling.
    /// Safe to call from other threads.
    /// 插入主循环任务队列
    void queueInLoop(Functor cb);

    size_t queueSize() const;
    // timerss
    ///
    /// Runs callback at 'time'.
    /// Safe to call from other threads.
    /// 某个时间点执行定时回调
    TimerId runAt(const Timestamp &time, TimerCallback cb);
    ///
    /// Runs callback after @c delay seconds.
    /// Safe to call from other threads.
    /// 某个时间点之后执行定时回调
    TimerId runAfter(double delay, TimerCallback cb);
    ///
    /// Runs callback every @c interval seconds.
    /// Safe to call from other threads.
    /// 在每个时间间隔处理某个回调函数
    TimerId runEvery(double interval, TimerCallback cb);
    ///
    /// Cancels the timer.
    /// Safe to call from other threads.
    /// 删除某个定时器
    void cancel(TimerId timerId);

    // internal usage
    void wakeup();                        //唤醒事件通知描述符
    void updateChannel(Channel *channel); //在poller中添加或者更新通道
    void removeChannel(Channel *channel); //在poller中移除通道
    bool hasChannel(Channel *channel);

    // pid_t threadId() const { return threadId_; }
    void assertInLoopThread() //如果不在I/O线程中则退出程序
    {
        if (!isInLoopThread())
        {
            abortNotInLoopThread();
        }
    }
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); } //检测是否在I/O线程中
    // bool callingPendingFunctors() const { return callingPendingFunctors_; }
    bool eventHandling() const { return eventHandling_; } //是否正在处理事件

    void setContext(const std::any &context)
    {
        context_ = context;
    }

    const std::any &getContext() const
    {
        return context_;
    }

    std::any *getMutableContext()
    {
        return &context_;
    }

    static EventLoop *getEventLoopOfCurrentThread(); //判断当前线程是否为I/O线程

private:
    void abortNotInLoopThread(); //不在主I/O线程,终止程序
    void handleRead();           // waked up,将事件通知描述符里的内容读走,以便让其继续检测事件通知
    void doPendingFunctors();    //执行转交给I/O的任务

    void printActiveChannels() const; // DEBUG,将发生的事件写入日志

    typedef std::vector<Channel *> ChannelList; //事件分发器列表

    bool looping_;                           /* atomic因为他是bool类型，在linux下bool类型是原子操作，所以不需要锁 */
    std::atomic<bool> quit_;                 /* atomic and shared between threads, okay on x86, I guess. 是否退出事件循环*/
    bool eventHandling_;                     /* atomic */
    bool callingPendingFunctors_;            /* atomic 处于io线程的计算操作位置*/
    int64_t iteration_;                      //事件循环的次数
    const pid_t threadId_;                   //当前对象所属线程id,运行loop的线程ID
    Timestamp pollReturnTime_;               //poll阻塞的时间
    std::unique_ptr<Poller> poller_;         //IO复用
    std::unique_ptr<TimerQueue> timerQueue_; //定时器队列
    int wakeupFd_;                           //用于eventfd,唤醒套接字
    // unlike in TimerQueue, which is an internal class,
    // we don't expose Channel to client.
    std::unique_ptr<Channel> wakeupChannel_; //unique_ptr,该通道会被纳入poller_来管理,封装事件描述符
    std::any context_;

    // scratch variables
    ChannelList activeChannels_;    //poller返回的活动通道
    Channel *currentActiveChannel_; //当前正在处理的活动通道

    mutable MutexLock mutex_;                                 //互斥锁
    std::vector<Functor> pendingFunctors_ GUARDED_BY(mutex_); //需要在主I/O线程执行的任务
};

} // namespace net
} // namespace muduo

#endif // MUDUO_NET_EVENTLOOP_H
