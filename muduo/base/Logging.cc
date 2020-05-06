// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Logging.h"

#include "muduo/base/CurrentThread.h"
#include "muduo/base/Timestamp.h"
#include "muduo/base/TimeZone.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sstream>

namespace muduo
{

/*
class LoggerImpl
{
 public:
  typedef Logger::LogLevel LogLevel;
  LoggerImpl(LogLevel level, int old_errno, const char* file, int line);
  void finish();

  Timestamp time_;
  LogStream stream_;
  LogLevel level_;
  int line_;
  const char* fullname_;
  const char* basename_;
};
*/
//整体流程
//整个操作过程：先构造无名的Logger对象，在通过这个对象调用stream()函数，
//在返回一个LogStream对象，在通过这个对象调运插入运算符
//logger类里面有=>impl类负责日志的格式化，类里面有=>logstream调用插入运算符=>operator<< 输出日志=>
//到缓冲区fixedbuffer类，当销毁Logger时=>在Logger匿名对象析构的时候调用g_output进行输出=>g_flush
__thread char t_errnobuf[512];
__thread char t_time[32];
__thread time_t t_lastSecond;

const char *strerror_tl(int savedErrno)
{
    return strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf);
}

Logger::LogLevel initLogLevel()
{
    if (::getenv("MUDUO_LOG_TRACE"))
        return Logger::TRACE;
    else if (::getenv("MUDUO_LOG_DEBUG"))
        return Logger::DEBUG;
    else
        return Logger::INFO;
}

Logger::LogLevel g_logLevel = initLogLevel();

const char *LogLevelName[Logger::NUM_LOG_LEVELS] =
    {
        "TRACE ",
        "DEBUG ",
        "INFO  ",
        "WARN  ",
        "ERROR ",
        "FATAL ",
};

// helper class for known string length at compile time
class T
{
public:
    T(const char *str, unsigned len)
        : str_(str),
          len_(len)
    {
        assert(strlen(str) == len_);
    }

    const char *str_;
    const unsigned len_;
};

inline LogStream &operator<<(LogStream &s, T v)
{
    s.append(v.str_, v.len_);
    return s;
}

inline LogStream &operator<<(LogStream &s, const Logger::SourceFile &v)
{
    s.append(v.data_, v.size_);
    return s;
}

void defaultOutput(const char *msg, int len)
{
    size_t n = fwrite(msg, 1, len, stdout);
    //FIXME check n
    (void)n;
}

void defaultFlush()
{
    fflush(stdout);
}

Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;
TimeZone g_logTimeZone;

} // namespace sserver

using namespace sserver;

Logger::Impl::Impl(LogLevel level, int savedErrno, const SourceFile &file, int line)
    : time_(Timestamp::now()),
      stream_(),
      level_(level),
      line_(line),
      basename_(file)
{
    formatTime();         //格式化时间
    CurrentThread::tid(); //当前线程的tid
    stream_ << T(CurrentThread::tidString(), CurrentThread::tidStringLength());
    stream_ << T(LogLevelName[level], 6);
    if (savedErrno != 0) //在把级别格式化进去
    {
        stream_ << strerror_tl(savedErrno) << " (errno=" << savedErrno << ") ";
    }
}

void Logger::Impl::formatTime() //格式化时间
{
    int64_t microSecondsSinceEpoch = time_.microSecondsSinceEpoch();
    time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / Timestamp::kMicroSecondsPerSecond);
    int microseconds = static_cast<int>(microSecondsSinceEpoch % Timestamp::kMicroSecondsPerSecond);
    if (seconds != t_lastSecond)
    {
        t_lastSecond = seconds;
        struct tm tm_time;
        if (g_logTimeZone.valid())
        {
            tm_time = g_logTimeZone.toLocalTime(seconds);
        }
        else
        {
            ::gmtime_r(&seconds, &tm_time); // FIXME TimeZone::fromUtcTime
        }

        int len = snprintf(t_time, sizeof(t_time), "%4d%02d%02d %02d:%02d:%02d",
                           tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                           tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec); //将时间格式化为这种格式
        assert(len == 17);
        (void)len;
    }

    if (g_logTimeZone.valid())
    {
        Fmt us(".%06d ", microseconds); //内部类做格式化的
        assert(us.length() == 8);
        stream_ << T(t_time, 17) << T(us.data(), 8); //先输出到缓冲区
    }
    else
    {
        Fmt us(".%06dZ ", microseconds);
        assert(us.length() == 9);
        stream_ << T(t_time, 17) << T(us.data(), 9);
    }
}

void Logger::Impl::finish()
{
    stream_ << " - " << basename_ << ':' << line_ << '\n';
}

Logger::Logger(SourceFile file, int line)
    : impl_(INFO, 0, file, line)
{
}

Logger::Logger(SourceFile file, int line, LogLevel level, const char *func)
    : impl_(level, 0, file, line) //嵌套类构造
{
    impl_.stream_ << func << ' '; //函数名称在此格式化
}

Logger::Logger(SourceFile file, int line, LogLevel level)
    : impl_(level, 0, file, line)
{
}

Logger::Logger(SourceFile file, int line, bool toAbort)
    : impl_(toAbort ? FATAL : ERROR, errno, file, line)
{
}

Logger::~Logger()
{
    impl_.finish();                                  //结束
    const LogStream::Buffer &buf(stream().buffer()); //把缓冲区取出来，保存到buf里
    g_output(buf.data(), buf.length());              //然后输出，默认是stdout
    if (impl_.level_ == FATAL)                       //级别高
    {
        g_flush(); //刷新缓冲区，到文件里
        abort();   //退出
    }
}

void Logger::setLogLevel(Logger::LogLevel level)
{
    g_logLevel = level;
}

void Logger::setOutput(OutputFunc out)
{
    g_output = out; //要修改输出的位置，更改为新的输出，默认是stdout
}

void Logger::setFlush(FlushFunc flush)
{
    g_flush = flush; //刷新流，把缓冲区的全要输出
}

void Logger::setTimeZone(const TimeZone &tz)
{
    g_logTimeZone = tz;
}
