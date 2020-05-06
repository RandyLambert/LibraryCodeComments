// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/ThreadPool.h"

#include "muduo/base/Exception.h"

#include <assert.h>
#include <stdio.h>

using namespace muduo;

ThreadPool::ThreadPool(const string& nameArg)
  : mutex_(),
    notEmpty_(mutex_),
    notFull_(mutex_),
    name_(nameArg),
    maxQueueSize_(0),
    running_(false)
{
}
ThreadPool::~ThreadPool() //析构函数，如果线程池在运行状态，就停止线程池
{
    if (running_) //如果线程池开始运行,如果线程池没在运行也不用调用stop()函数
    {
        stop();
    }
}

void ThreadPool::start(int numThreads) //参数为线程数量,会创建相应数量的线程,线程函数为ThreadPool::runInThread
{
    assert(threads_.empty());            //断言线程池为空
    running_ = true;                     //设定为true,作为线程启动标志
    threads_.reserve(numThreads);        //预先申请线程数的内存
    for (int i = 0; i < numThreads; ++i) //创建足够的线程
    {
        char id[32];
        snprintf(id, sizeof id, "%d", i + 1);                        //将线程id写到字符串中
        threads_.push_back(new sserver::Thread(                      //创建线程加入线程vector，线程名叫做线程池的名字+id
            std::bind(&ThreadPool::runInThread, this), name_ + id)); //绑定runInThread为线程运行函数
        threads_[i].start();                                         //启动线程
    }
    if (numThreads == 0 && threadInitCallback_) //如果线程池为空，且有回调函数，则调用回调函数。这时相当与只有一个主线程
    {
        threadInitCallback_();
    }
}

void ThreadPool::stop()
{
    {
        MutexLockGuard lock(mutex_); //获取锁的保护
        running_ = false;
        notEmpty_.notifyAll(); //通知所有等待线程，但是因为running变为了false，所以线程结束
    }
    for (auto &thr : threads_)
    {
        thr.join();
    }
}

size_t ThreadPool::queueSize() const
{
    MutexLockGuard lock(mutex_);
    return queue_.size();
}

void ThreadPool::run(Task task) //向线程池添加task
{
    if (threads_.empty()) //如果线程池是空的，那么直接由当前线程执行任务
    {
        task(); //如果没有子线程，就在主线程中执行该task
    }
    else
    {
        MutexLockGuard lock(mutex_); //用锁保护
        while (isFull())             //如果task队列queue_满了，就等待
        {
            notFull_.wait();
        }
        assert(!isFull()); //断言没满

        queue_.push_back(task); //将任务加入队列
        notEmpty_.notify();     //当添加了某个任务之后,任务队列肯定不是空的,通知某个等待从queue_中取task的线程
    }
}

ThreadPool::Task ThreadPool::take() //获取一个task
{
    //任务队列需要保护
    MutexLockGuard lock(mutex_);
    // always use a while-loop, due to spurious wakeup
    while (queue_.empty() && running_) //等待的条件有两种，要么是有任务到来，要摸就是线程池结束
    {
        notEmpty_.wait(); //没有任务，则等待（利用条件变量）
    }
    Task task;
    if (!queue_.empty()) //一旦有任务到来，任务队列不为空
    {
        task = queue_.front(); //弹出任务
        queue_.pop_front();
        if (maxQueueSize_ > 0) //判断这个线程池任务队列最大数目大小大于0,说明已经设定过大小,防止线程没有在start前设定maxQueueSize_
        {
            notFull_.notify(); //当解决了一个任务之后,任务队列肯定不是满的,通知某个等待向队列放入task线程。
        }
    }
    return task; //取出任务
}

bool ThreadPool::isFull() const //判断队列是否已满
{
    mutex_.assertLocked(); //断定被当前线程锁住
    return maxQueueSize_ > 0 && queue_.size() >= maxQueueSize_;
}

void ThreadPool::runInThread() //线程函数
{
    try
    {
        if (threadInitCallback_) //如果设置了就执行，在线程真正运行函数之前，进行一些初始化设置
        {
            threadInitCallback_();
        }
        while (running_) //如果running为true，则在这个循环里执行线程
        {
            Task task(take()); //获取任务
            if (task)
            {
                task(); //执行该任务
            }
        }
    }
    catch (const Exception &ex) //异常捕捉
    {
        fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
        fprintf(stderr, "reason: %s\n", ex.what());
        fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
        abort();
    }
    catch (const std::exception &ex)
    {
        fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
        fprintf(stderr, "reason: %s\n", ex.what());
        abort();
    }
    catch (...)
    {
        fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
        throw; // rethrow
    }
}
