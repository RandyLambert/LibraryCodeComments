// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Thread.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Exception.h"
#include "muduo/base/Logging.h"

#include <type_traits>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/unistd.h>

namespace muduo
{
namespace detail
{

pid_t gettid()
{
    return static_cast<pid_t>(::syscall(SYS_gettid)); //系统调用获取线程真实tid
}

void afterFork()
{
    sserver::CurrentThread::t_cachedTid = 0;
    sserver::CurrentThread::t_threadName = "main";
    CurrentThread::tid();
    //该函数是使用fork()函数之后，对子进程进行初始化
    // no need to call pthread_atfork(NULL, NULL, &afterFork);
}

class ThreadNameInitializer
{ //该类的作用就是给初始化，当一个子线程调用fork后，他就是新进程的主线程，所以改线程名为main
public:
    ThreadNameInitializer()
    {
        sserver::CurrentThread::t_threadName = "main";
        CurrentThread::tid();
        pthread_atfork(NULL, NULL, &afterFork); //给主线程一个属性，当他调用fork()后，会给子进程也进行获取tid等操作
        //不建议多进程+多线程写，可能会产生死锁情况
        //int pthread_atfork(void (*propare)(void),void (*parent)(void),void(*child)(void))
        //调用fork时，内部创建了子进程前在父进程中会调用的prepare，内部创建子进程成功后
        //父进程会调用parent，子进程会调用child
    }
};

ThreadNameInitializer init;
//相当于一个全局变量，这个对象的构造先与main函数，一开始就进入到构造函数之中
//然后将主线程的名称改为main，缓存当前线程的tid到当前变量中

struct ThreadData //一个结构体，做为参数如pthread_create去，相当于直接传进去一个ThreadData类
{
    typedef sserver::Thread::ThreadFunc ThreadFunc;
    ThreadFunc func_;
    string name_;
    pid_t *tid_;
    CountDownLatch *latch_;

    ThreadData(ThreadFunc func,
               const string &name,
               pid_t *tid,
               CountDownLatch *latch)
        : func_(std::move(func)),
          name_(name),
          tid_(tid),
          latch_(latch)
    {
    }

    void runInThread() //真正创建线程后,穿进去的函数会直接调用他
    {
        *tid_ = sserver::CurrentThread::tid();
        tid_ = NULL;
        latch_->countDown();
        latch_ = NULL;

        sserver::CurrentThread::t_threadName = name_.empty() ? "sserverThread" : name_.c_str();
        ::prctl(PR_SET_NAME, sserver::CurrentThread::t_threadName);

        try
        {
            func_();
            sserver::CurrentThread::t_threadName = "finished";
        }
        catch (const Exception &ex) //异常捕捉，先在自己写的函数，在是函数库，最后是不得已的捕捉
        {
            sserver::CurrentThread::t_threadName = "crashed";
            fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
            fprintf(stderr, "reason: %s\n", ex.what());
            fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
            abort();
        }
        catch (const std::exception &ex)
        {
            sserver::CurrentThread::t_threadName = "crashed";
            fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
            fprintf(stderr, "reason: %s\n", ex.what());
            abort();
        }
        catch (...)
        {
            sserver::CurrentThread::t_threadName = "crashed";
            fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
            throw; // rethrow
        }
    }
};
/*****************************************************************************/
void *startThread(void *obj)
{
    ThreadData *data = static_cast<ThreadData *>(obj);
    data->runInThread();
    delete data;
    return NULL;
}

} // namespace detail
} // namespace sserver

using namespace sserver;

void CurrentThread::cacheTid() //缓存tid
{
    if (t_cachedTid == 0)
    {
        t_cachedTid = detail::gettid();                                                     //通过系统调用，将tid获取
        t_tidStringLength = snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid); //格式化pid到字符串
    }
}

bool CurrentThread::isMainThread()
{
    return tid() == getpid(); //判断这个线程的tid是不是主线程的pid，如果是，就说明是主线程
}

void CurrentThread::sleepUsec(int64_t usec)
{
    struct timespec ts = {0, 0};
    ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
    ts.tv_nsec = static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
    ::nanosleep(&ts, NULL); //nanosleep有好处，使用usleep会影响系统调用
}

/********************************************************************************/
AtomicInt32 Thread::numCreated_;

Thread::Thread(ThreadFunc func, const string &n)
    : started_(false),
      joined_(false),
      pthreadId_(0),
      tid_(0),
      func_(std::move(func)),
      name_(n),
      latch_(1)
{
    setDefaultName();
}

Thread::~Thread()
{
    if (started_ && !joined_) //判断可以析构
    {
        pthread_detach(pthreadId_);
    }
}

void Thread::setDefaultName()
{
    int num = numCreated_.incrementAndGet(); //原子操作，自增一个线程
    if (name_.empty())                       //如果这个线程没有被命名，则默认给线程的名字叫Thread
    {
        char buf[32];
        snprintf(buf, sizeof buf, "Thread%d", num);
        name_ = buf;
    }
}
//这里有一个宏，值得注意
void Thread::start() //线程开始
{
    assert(!started_);
    started_ = true;
    detail::ThreadData *data = new detail::ThreadData(func_, name_, &tid_, &latch_); //作为参数传进去
    if (pthread_create(&pthreadId_, NULL, &detail::startThread, data))               //线程的入口函数
    {
        started_ = false;
        delete data;                                // or no delete?
        LOG_SYSFATAL << "Failed in pthread_create"; //日志
    }
    else
    {
        latch_.wait();
        assert(tid_ > 0);
    }
}

int Thread::join()
{
    assert(started_); //断定线程已经打开
    assert(!joined_); //要在线程打开的并且还没有join的时候join
    joined_ = true;
    return pthread_join(pthreadId_, NULL);
}

