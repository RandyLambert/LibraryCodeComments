// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/LogFile.h"

#include "muduo/base/FileUtil.h"
#include "muduo/base/ProcessInfo.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

using namespace muduo;

LogFile::LogFile(const std::string &basename, //线程安全的
                 size_t rollSize,
                 bool threadSafe,
                 int flushInterval,
                 int checkEveryN)
    : basename_(basename),
      rollSize_(rollSize),
      flushInterval_(flushInterval),
      checkEveryN_(checkEveryN),
      count_(0),
      mutex_(threadSafe ? new MutexLock : NULL), //智能指针，所以不需要delete
      startOfPeriod_(0),
      lastRoll_(0),
      lastFlush_(0)
{
    assert(basename.find('/') == string::npos); //断言
    rollFile();                                 //滚动一个日志，也就是获取一个文件
}

LogFile::~LogFile() = default;

void LogFile::append(const char *logline, int len)
{
    if (mutex_) //线程安全
    {
        MutexLockGuard lock(*mutex_);
        append_unlocked(logline, len);
    }
    else
    {
        append_unlocked(logline, len);
    }
}

void LogFile::flush() //刷新缓冲区
{
    if (mutex_)
    {
        MutexLockGuard lock(*mutex_);
        file_->flush();
    }
    else
    {
        file_->flush();
    }
}

void LogFile::append_unlocked(const char *logline, int len)
{
    file_->append(logline, len); //一旦写入文件

    if (file_->writtenBytes() > rollSize_) //就要判断是否需要滚动日志
    {
        rollFile();
    }
    else
    {
        ++count_;
        if (count_ >= checkEveryN_) //第二个需要滚动的条件：时间
        {
            count_ = 0;
            time_t now = ::time(NULL);
            time_t thisPeriod_ = now / kRollPerSeconds_ * kRollPerSeconds_; //调整到0点
            if (thisPeriod_ != startOfPeriod_)                              //如果不等，说明是第二天了，就应该滚动了
            {
                rollFile();
            }
            else if (now - lastFlush_ > flushInterval_) //判断是否应该刷新
            {
                lastFlush_ = now;
                file_->flush();
            }
        }
    }
}

bool LogFile::rollFile()
{
    time_t now = 0;
    string filename = getLogFileName(basename_, &now);
    //这里是对其到krollperseconds的整数倍，也就是时间调整到当天的0时
    time_t start = now / kRollPerSeconds_ * kRollPerSeconds_; //取整的操作

    if (now > lastRoll_)
    {
        lastRoll_ = now;
        lastFlush_ = now;
        startOfPeriod_ = start;
        file_.reset(new FileUtil::AppendFile(filename));
        return true;
    }
    return false;
}

string LogFile::getLogFileName(const string &basename, time_t *now)
{ //获取文件名
    string filename;
    filename.reserve(basename.size() + 64); //预设文件名大小
    filename = basename;

    char timebuf[32];
    struct tm tm;
    *now = time(NULL);
    gmtime_r(now, &tm); // FIXME: localtime_r ? gmtime_r是线程安全的，就算now内的指针内容被改变了，还可以获取tm中的内容
    strftime(timebuf, sizeof timebuf, ".%Y%m%d-%H%M%S.", &tm);
    filename += timebuf;

    filename += ProcessInfo::hostname();

    char pidbuf[32];
    snprintf(pidbuf, sizeof pidbuf, ".%d", ProcessInfo::pid()); //添加进程号
    filename += pidbuf;

    filename += ".log";

    return filename;
}
