// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGFILE_H
#define MUDUO_BASE_LOGFILE_H

#include "muduo/base/Mutex.h"
#include "muduo/base/Types.h"

#include <memory>

namespace muduo
{

namespace FileUtil
{
class AppendFile;
}

class LogFile : noncopyable
{ //LogFile进一步封装了FileUtil，并设置了一个循环次数，没过这么多次就flush一次。
public:
    LogFile(const string &basename,
            off_t rollSize,
            bool threadSafe = true,
            int flushInterval = 3,
            int checkEveryN = 1024);
    ~LogFile();

    void append(const char *logline, int len); //添加
    void flush();                              //刷新缓冲好区
    bool rollFile();                           //滚动日志

private:
    void append_unlocked(const char *logline, int len); //不加锁的方案添加

    static std::string getLogFileName(const std::string &basename, time_t *now); //获取日志文件名称

    const std::string basename_; //日志文件 basename
    const size_t rollSize_;      //日志文件达到了一个rollsize换一个新文件
    const int flushInterval_;    //日志写入间隔时间，日志是间隔一段时间才写入，不然开销太大
    const int checkEveryN_;

    int count_;

    std::unique_ptr<MutexLock> mutex_;
    time_t startOfPeriod_;                       //开始记录日志时间（将会调整到0点的时间）
    time_t lastRoll_;                            //上一次滚动入职文件的时间
    time_t lastFlush_;                           //上一次日志写入文件的时间
    std::unique_ptr<FileUtil::AppendFile> file_; //前向声明

    const static int kRollPerSeconds_ = 60 * 60 * 24; //滚动日志时间
};

} // namespace muduo
#endif // MUDUO_BASE_LOGFILE_H
