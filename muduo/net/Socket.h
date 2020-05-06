// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_SOCKET_H
#define MUDUO_NET_SOCKET_H

#include "muduo/base/noncopyable.h"

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
///
/// TCP networking.
///
namespace net
{

class InetAddress;

///
/// Wrapper of socket file descriptor.
///
/// It closes the sockfd when desctructs.
/// It's thread safe, all operations are delagated to OS.
class Socket : noncopyable
{
 //封装的套接字类，以类的形式管理套接字
public:
    explicit Socket(int sockfd)
        : sockfd_(sockfd)
    {
    }

    // Socket(Socket&&) // move constructor in C++11
    ~Socket(); //调用close，不会忘记关闭文件描述符
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    int fd() const { return sockfd_; } //返回fd
    // return true if success.
    bool getTcpInfo(struct tcp_info *) const;
    bool getTcpInfoString(char *buf, int len) const;

    /// abort if address in use
    void bindAddress(const InetAddress &localaddr); //监听套接字和地址绑定
    /// abort if address in use
    void listen(); //监听

    /// On success, returns a non-negative integer that is
    /// a descriptor for the accepted socket, which has been
    /// set to non-blocking and close-on-exec. *peeraddr is assigned.
    /// On error, -1 is returned, and *peeraddr is untouched.
    int accept(InetAddress *peeraddr); //连接

    void shutdownWrite();

    ///
    /// Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
    ///nagle算法可以一定程度上避免网络拥塞
    ///tcp_nodelay选项可以禁用nagle算法
    ///禁用nagle算法可以避免连续发包出现延迟，这对编写低延迟的网络服务很重要
    void setTcpNoDelay(bool on);

    ///
    /// Enable/disable SO_REUSEADDR
    ///
    void setReuseAddr(bool on);

    ///
    /// Enable/disable SO_REUSEPORT
    ///
    void setReusePort(bool on);

    ///
    /// Enable/disable SO_KEEPALIVE
    ///tcp keepalive是指定期探测连接是否存在，如果应用层有心跳包，这个选项可以不必设置
    void setKeepAlive(bool on);

private:
    const int sockfd_; //就一个变量sockfd_
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_SOCKET_H
