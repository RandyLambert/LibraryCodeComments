// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Buffer.h"

#include "muduo/net/SocketsOps.h"

#include <errno.h>
#include <sys/uio.h>

using namespace muduo;
using namespace muduo::net;

const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

//结合栈上的空间，避免内存使用过大，提高内存使用率
//如果有5k个连接，每个连接就分配64k+64k的缓冲区的话，将占用640m
//而大多数的时候，这些缓冲区的使用率很低
ssize_t Buffer::readFd(int fd, int *savedErrno)
{
    // saved an ioctl()/FIONREAD call to tell how much to read
    //节省了一次ioctl系统调用（获取有多少可读数据）
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writableBytes();
    //第一块缓冲区
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;
    //第二块缓冲区
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;
    // when there is enough space in this buffer, don't read into extrabuf.
    // when extrabuf is used, we read 128k-1 bytes at most.
    const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
    const ssize_t n = sockets::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *savedErrno = errno;
    }
    else if (static_cast<size_t>(n) <= writable)
    {
        writerIndex_ += n;
    }
    else
    {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    // if (n == writable + sizeof extrabuf)
    // {
    //   goto line_30;
    // }
    return n;
}
