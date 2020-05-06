// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/SocketsOps.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include "muduo/net/Endian.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>  // snprintf
#include <sys/socket.h>
#include <sys/uio.h>  // readv
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{

typedef struct sockaddr SA;


#if VALGRIND || defined (NO_ACCEPT4)
void setNonBlockAndCloseOnExec(int sockfd)
{
  // non-block
  int flags = ::fcntl(sockfd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  int ret = ::fcntl(sockfd, F_SETFL, flags);
  // FIXME check

  // close-on-exec
  flags = ::fcntl(sockfd, F_GETFD, 0);
  flags |= FD_CLOEXEC;
  ret = ::fcntl(sockfd, F_SETFD, flags);
  // FIXME check

  (void)ret;
}
#endif

}  // namespace

const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in6* addr)
{
  return static_cast<const struct sockaddr*>(implicit_cast<const void*>(addr));
}

struct sockaddr* sockets::sockaddr_cast(struct sockaddr_in6* addr)
{
  return static_cast<struct sockaddr*>(implicit_cast<void*>(addr));
}

const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in* addr)
{
  return static_cast<const struct sockaddr*>(implicit_cast<const void*>(addr));
}

const struct sockaddr_in* sockets::sockaddr_in_cast(const struct sockaddr* addr)
{
  return static_cast<const struct sockaddr_in*>(implicit_cast<const void*>(addr));
}

const struct sockaddr_in6* sockets::sockaddr_in6_cast(const struct sockaddr* addr)
{
  return static_cast<const struct sockaddr_in6*>(implicit_cast<const void*>(addr));
}

int sockets::createNonblockingOrDie(sa_family_t family) //创建非阻塞套接字，创建失败就终止
{
     //linux 2.6.27以上的内核支持sock_nonblock和sock_cloexec的检测，就不需要上面的set函数了
#if VALGRIND
  int sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (sockfd < 0)
  {
    LOG_SYSFATAL << "sockets::createNonblockingOrDie";
  }

  setNonBlockAndCloseOnExec(sockfd);
#else
  int sockfd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
  if (sockfd < 0)
  {
    LOG_SYSFATAL << "sockets::createNonblockingOrDie";
  }
#endif
  return sockfd;
}

void sockets::bindOrDie(int sockfd, const struct sockaddr* addr)
{
  int ret = ::bind(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
  if (ret < 0)
  {
    LOG_SYSFATAL << "sockets::bindOrDie";
  }
}

void sockets::listenOrDie(int sockfd)
{
  int ret = ::listen(sockfd, SOMAXCONN);
  if (ret < 0)
  {
    LOG_SYSFATAL << "sockets::listenOrDie";
  }
}

int sockets::accept(int sockfd, struct sockaddr_in *addr)
{
    socklen_t addrlen = static_cast<socklen_t>(sizeof *addr);
    int connfd = ::accept4(sockfd, sockaddr_cast(addr), //新的系统调用，这个已连接的套接字已经是非阻塞的了
                           &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd < 0)
    {
        int savedErrno = errno;         //先保存错误代码
        LOG_SYSERR << "Socket::accept"; //因为这里登记了一个错误，所以调用前先保存起来errno
        switch (savedErrno)
        {
        case EAGAIN:
        case ECONNABORTED:
        case EINTR:
        case EPROTO: // ???
        case EPERM:
        case EMFILE: // per-process lmit of open file desctiptor ???
            // expected errors
            errno = savedErrno; //将errno变回之前
            break;              //不是致命错误
        case EBADF:
        case EFAULT:
        case EINVAL:
        case ENFILE:
        case ENOBUFS:
        case ENOMEM:
        case ENOTSOCK:
        case EOPNOTSUPP:
            // 致命错误，登记日志，退出
            LOG_FATAL << "unexpected error of ::accept " << savedErrno;
            break;
        default: //未知错误，致命
            LOG_FATAL << "unknown error of ::accept " << savedErrno;
            break;
        }
    }
    return connfd;
}

int sockets::connect(int sockfd, const struct sockaddr* addr)
{
  return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
}

ssize_t sockets::read(int sockfd, void *buf, size_t count)
{
  return ::read(sockfd, buf, count);
}

ssize_t sockets::readv(int sockfd, const struct iovec *iov, int iovcnt)
{
    return ::readv(sockfd, iov, iovcnt); //和read的区别是，接受的数据可以填充到多个缓冲区中
}

ssize_t sockets::write(int sockfd, const void *buf, size_t count)
{
    return ::write(sockfd, buf, count); //writev没封装
}

void sockets::close(int sockfd)
{
  if (::close(sockfd) < 0)
  {
    LOG_SYSERR << "sockets::close";
  }
}

//只关闭写的一段
void sockets::shutdownWrite(int sockfd)
{
    if (::shutdown(sockfd, SHUT_WR) < 0)
    {
        LOG_SYSERR << "sockets::shutdownWrite";
    }
}
//将地址转换为ip和端口的形式
void sockets::toIpPort(char *buf, size_t size,
                       const struct sockaddr_in &addr)
{
    assert(size >= INET_ADDRSTRLEN); //宏，网际地址的长度，提供的缓冲区大小一定要大于网际地址的长度
    ::inet_ntop(AF_INET, &addr.sin_addr, buf, static_cast<socklen_t>(size));
    size_t end = ::strlen(buf);
    uint16_t port = sockets::networkToHost16(addr.sin_port); //将网络字节序的端口转换为主机字节序的端口
    assert(size > end);
    snprintf(buf + end, size - end, ":%u", port); //将地址端口结构化
}

void sockets::toIp(char *buf, size_t size,
                   const struct sockaddr_in &addr)
{
    assert(size >= INET_ADDRSTRLEN); //宏，网际地址的长度，提供的缓冲区大小一定要大于网际地址的长度
    ::inet_ntop(AF_INET, &addr.sin_addr, buf, static_cast<socklen_t>(size));
}

void sockets::fromIpPort(const char *ip, uint16_t port, //从ip和端口，变成一个addr
                         struct sockaddr_in *addr)
{
    addr->sin_family = AF_INET;
    addr->sin_port = hostToNetwork16(port);
    if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0) //从主机变成网际
    {
        LOG_SYSERR << "sockets::fromIpPort";
    }
}

int sockets::getSocketError(int sockfd) //返回socket错误
{
    int optval;
    socklen_t optlen = static_cast<socklen_t>(sizeof optval);

    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) //返回socket错误，返回到optval中
    {
        return errno;
    }
    else
    {
        return optval;
    }
}

struct sockaddr_in sockets::getLocalAddr(int sockfd) //对于每个以连接套接字，都会有个本机地址和对等方的地址
{
    struct sockaddr_in localaddr;
    bzero(&localaddr, sizeof localaddr);
    socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
    if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0) //获取本机地址
    {
        LOG_SYSERR << "sockets::getLocalAddr";
    }
    return localaddr;
}

struct sockaddr_in sockets::getPeerAddr(int sockfd)
{
    struct sockaddr_in peeraddr;
    bzero(&peeraddr, sizeof peeraddr);
    socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
    if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0) //获取对方地址
    {
        LOG_SYSERR << "sockets::getPeerAddr";
    }
    return peeraddr;
}

bool sockets::isSelfConnect(int sockfd)
{
  struct sockaddr_in6 localaddr = getLocalAddr(sockfd);
  struct sockaddr_in6 peeraddr = getPeerAddr(sockfd);
  if (localaddr.sin6_family == AF_INET)
  {
    const struct sockaddr_in* laddr4 = reinterpret_cast<struct sockaddr_in*>(&localaddr);
    const struct sockaddr_in* raddr4 = reinterpret_cast<struct sockaddr_in*>(&peeraddr);
    return laddr4->sin_port == raddr4->sin_port
        && laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
  }
  else if (localaddr.sin6_family == AF_INET6)
  {
    return localaddr.sin6_port == peeraddr.sin6_port
        && memcmp(&localaddr.sin6_addr, &peeraddr.sin6_addr, sizeof localaddr.sin6_addr) == 0;
  }
  else
  {
    return false;
  }
}

