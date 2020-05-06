// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_INETADDRESS_H
#define MUDUO_NET_INETADDRESS_H

#include "muduo/base/copyable.h"
#include "muduo/base/StringPiece.h"

#include <netinet/in.h>

namespace muduo
{
namespace net
{
namespace sockets
{
const struct sockaddr *sockaddr_cast(const struct sockaddr_in6 *addr);
}

/// 对于网际地址的封装
/// Wrapper of sockaddr_in.
///
/// This is an POD interface class.
class InetAddress : public muduo::copyable
{
public:
    /// Constructs an endpoint with given port number.
    /// Mostly used in TcpServer listening.
    /// 仅仅只是指定了port，不指定ip，则ip为INADDR_ANY
    explicit InetAddress(uint16_t port = 0, bool loopbackOnly = false, bool ipv6 = false); //构造一个地址，ip和端口都指定

    /// Constructs an endpoint with given ip and port.
    /// @c ip should be "1.2.3.4"
    InetAddress(StringArg ip, uint16_t port, bool ipv6 = false);

    /// Constructs an endpoint with given struct @c sockaddr_in
    /// Mostly used when accepting new connections
    explicit InetAddress(const struct sockaddr_in &addr)
        : addr_(addr) //对socketaddrin的封装
    {
    }

    explicit InetAddress(const struct sockaddr_in6 &addr)
        : addr6_(addr)
    {
    }

    sa_family_t family() const { return addr_.sin_family; }
    std::string toIp() const;     //转为ip
    std::string toIpPort() const; //转为ip端口
    uint16_t toPort() const;

    // default copy/assignment are Okay

    const struct sockaddr *getSockAddr() const { return sockets::sockaddr_cast(&addr6_); }
    void setSockAddrInet6(const struct sockaddr_in6 &addr6) { addr6_ = addr6; }

    uint32_t ipNetEndian() const { return addr_.sin_addr.s_addr; } //返回网络字节序的32位ip
    uint16_t portNetEndian() const { return addr_.sin_port; }      //返回网络字节序的端口

    // resolve hostname to IP address, not changing port or sin_family
    // return true on success.
    // thread safe
    static bool resolve(StringArg hostname, InetAddress *result);
    // static std::vector<InetAddress> resolveAll(const char* hostname, uint16_t port = 0);

    // set IPv6 ScopeID
    void setScopeId(uint32_t scope_id);

private:
    union {
        struct sockaddr_in addr_;
        struct sockaddr_in6 addr6_;
    };
};

} // namespace net
} // namespace muduo

#endif // MUDUO_NET_INETADDRESS_H
