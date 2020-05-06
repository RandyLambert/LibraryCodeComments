// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/FileUtil.h"
#include "muduo/base/Logging.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace muduo;

//I/o总线 能不能并行
//多个线程对同一个文件进行写入，效率可能不如单个线程对同一个文件写入效率高
//所以采用异部日志的形式

FileUtil::AppendFile::AppendFile(StringArg filename)
    : fp_(::fopen(filename.c_str(), "ae")), // 'e' for O_CLOEXEC
      writtenBytes_(0)
{
    assert(fp_);
    ::setbuffer(fp_, buffer_, sizeof buffer_);
    // posix_fadvise POSIX_FADV_DONTNEED ?
}

FileUtil::AppendFile::~AppendFile()
{
    ::fclose(fp_); //关闭文件指针
}

void FileUtil::AppendFile::append(const char *logline, const size_t len) //len是一共要写的个数
{
    size_t n = write(logline, len); //调用内部成员函数，写入
    size_t remain = len - n;
    while (remain > 0) //一个循环，如果没有写完，就继续写
    {
        size_t x = write(logline + n, remain);
        if (x == 0)
        {
            int err = ferror(fp_);
            if (err)
            {
                fprintf(stderr, "AppendFile::append() failed %s\n", strerror_tl(err));
            }
            break;
        }
        n += x;
        remain = len - n; // remain -= x
    }

    writtenBytes_ += len;
}

void FileUtil::AppendFile::flush()
{
    ::fflush(fp_);
}

size_t FileUtil::AppendFile::write(const char *logline, size_t len)
{
    // #undef fwrite_unlocked线程不安全
    return ::fwrite_unlocked(logline, 1, len, fp_); //使用不加锁的方式写入，除了这点，其他和fwrite是一样的
}

FileUtil::ReadSmallFile::ReadSmallFile(StringArg filename)
    : fd_(::open(filename.c_str(), O_RDONLY | O_CLOEXEC)),
      err_(0)
{
    buf_[0] = '\0';
    if (fd_ < 0)
    {
        err_ = errno;
    }
}

FileUtil::ReadSmallFile::~ReadSmallFile()
{
    if (fd_ >= 0)
    {
        ::close(fd_); // FIXME: check EINTR
    }
}

// return errno
template <typename String>
int FileUtil::ReadSmallFile::readToString(int maxSize, //采用的是系统调用的度文件
                                          String *content,
                                          int64_t *fileSize, //指针不为空
                                          int64_t *modifyTime,
                                          int64_t *createTime)
{
    static_assert(sizeof(off_t) == 8, "not 64");
    assert(content != NULL);
    int err = err_;
    if (fd_ >= 0)
    {
        content->clear();

        if (fileSize)
        {
            struct stat statbuf;
            if (::fstat(fd_, &statbuf) == 0) //获取文件大小
            {
                if (S_ISREG(statbuf.st_mode))
                {
                    *fileSize = statbuf.st_size;
                    content->reserve(static_cast<int>(std::min(static_cast<int64_t>(maxSize), *fileSize)));
                }
                else if (S_ISDIR(statbuf.st_mode))
                {
                    err = EISDIR;
                }
                if (modifyTime)
                {
                    *modifyTime = statbuf.st_mtime; //文件修该时间
                }
                if (createTime)
                {
                    *createTime = statbuf.st_ctime; //文件创建时间
                }
            }
            else
            {
                err = errno;
            }
        }

        while (content->size() < static_cast<size_t>(maxSize))
        {
            size_t toRead = std::min(static_cast<size_t>(maxSize) - content->size(), sizeof(buf_));
            ssize_t n = ::read(fd_, buf_, toRead);
            if (n > 0)
            {
                content->append(buf_, n);
            }
            else
            {
                if (n < 0)
                {
                    err = errno;
                }
                break;
            }
        }
    }
    return err;
}

int FileUtil::ReadSmallFile::readToBuffer(int *size)
{
    int err = err_;
    if (fd_ >= 0)
    {
        ssize_t n = ::pread(fd_, buf_, sizeof(buf_) - 1, 0); //调用pread函数，他可以从一个指定的偏移位置读到buffer
        if (n >= 0)
        {
            if (size)
            {
                *size = static_cast<int>(n);
            }
            buf_[n] = '\0';
        }
        else
        {
            err = errno;
        }
    }
    return err;
}

template int FileUtil::readFile(StringArg filename,
                                int maxSize,
                                std::string *content,
                                int64_t *, int64_t *, int64_t *);

template int FileUtil::ReadSmallFile::readToString(
    int maxSize,
    std::string *content,
    int64_t *, int64_t *, int64_t *);
