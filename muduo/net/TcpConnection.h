// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPCONNECTION_H
#define MUDUO_NET_TCPCONNECTION_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/StringPiece.h"
#include "muduo/base/Types.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/InetAddress.h"

#include <memory>

#include <boost/any.hpp>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
namespace net
{

class Channel;
class EventLoop;
class Socket;

///
/// TCP connection, for both client and server usage.
///
/// This is an interface class, so don't expose too much details.
///当连接到来的时候，创建一个tcpconnection对象，立刻用shared_ptr来管理
///引用计数为1，在channel中维护一个weak_ptr(tie_)，将这个shared_ptr
///对象赋值个tie_,引用技术仍为1，当连接关闭时，将tie_提升，得到一个shaped_ptr
///对象，引用计数就变为2
///
///
class TcpConnection : noncopyable,
                      public std::enable_shared_from_this<TcpConnection>
{
public:
    /// Constructs a TcpConnection with a connected sockfd
    ///
    /// User should not create this object.
    TcpConnection(EventLoop *loop,
                  const string &name,
                  int sockfd,
                  const InetAddress &localAddr,
                  const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; }
    const string &name() const { return name_; }
    const InetAddress &localAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }
    bool connected() const { return state_ == kConnected; }
    bool disconnected() const { return state_ == kDisconnected; }
    // return true if success.
    bool getTcpInfo(struct tcp_info *) const;
    string getTcpInfoString() const;

    // void send(string&& message); // C++11
    void send(const void *message, int len);
    void send(const StringPiece &message);
    // void send(Buffer&& message); // C++11
    void send(Buffer *message); // this one will swap data
    void shutdown();            // NOT thread safe, no simultaneous calling
    // void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling
    void forceClose();
    void forceCloseWithDelay(double seconds);
    void setTcpNoDelay(bool on);
    // reading or not
    void startRead();
    void stopRead();
    bool isReading() const { return reading_; }; // NOT thread safe, may race with start/stopReadInLoop

    void setContext(const std::any &context) //把一个未知类型赋值
    {
        context_ = context;
    }

    const std::any &getContext() const //获取未知类型，不能更改
    {
        return context_;
    }

    std::any *getMutableContext() //get可变的，可以更改
    {
        return &context_;
    }

    void setConnectionCallback(const ConnectionCallback &cb)
    {
        connectionCallback_ = cb;
    }

    void setMessageCallback(const MessageCallback &cb)
    {
        messageCallback_ = cb;
    }

    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    {
        writeCompleteCallback_ = cb;
    }

    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highWaterMark)
    {
        highWaterMarkCallback_ = cb;
        highWaterMark_ = highWaterMark;
    }

    /// Advanced interface
    Buffer *inputBuffer()
    {
        return &inputBuffer_;
    }

    Buffer *outputBuffer()
    {
        return &outputBuffer_;
    }

    /// Internal use only.
    void setCloseCallback(const CloseCallback &cb)
    {
        closeCallback_ = cb;
    }

    // called when TcpServer accepts a new connection
    void connectEstablished(); // should be called only once
    // called when TcpServer has removed me from its map
    void connectDestroyed(); // should be called only once

private:
    enum StateE //连接的状态
    {
        kDisconnected,
        kConnecting,
        kConnected,
        kDisconnecting
    };
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();
    // void sendInLoop(string&& message);
    void sendInLoop(const StringPiece &message);
    void sendInLoop(const void *message, size_t len);
    void shutdownInLoop();
    // void shutdownAndForceCloseInLoop(double seconds);
    void forceCloseInLoop();
    void setState(StateE s) { state_ = s; }
    const char *stateToString() const;
    void startReadInLoop();
    void stopReadInLoop();

    EventLoop *loop_;        //所属eventloop
    const std::string name_; //连接名
    StateE state_;           // FIXME: use atomic variable
    // we don't expose those classes to client.
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;
    const InetAddress localAddr_;
    const InetAddress peerAddr_;
    ConnectionCallback connectionCallback_;       //从tcpserver中的setconnect..back来的
    MessageCallback messageCallback_;             //从tcpserver中的setconnect..back里来的
    WriteCompleteCallback writeCompleteCallback_; //数据发送完毕的回调函数，即所以的用户数据都已拷贝到内核缓冲区时
    //回调该函数，outputbuffer_被清空也会回调该函数，可以理解为低水位标回调函数。        大流量才需要这个回调函数
    //大流量
    //不断生成数据，然后用发送conn—>send();
    //如果对等方接受不及时，受到通告窗口的控制，内核发送缓冲区不足，这个时候，就会将用户数据添加到应用层
    //发送缓冲区(output buffer);可能会承包output buffer。
    //解决方法就是，调整发送频率。
    //关注writecomplecallback。这样当所有用户数据拷贝到内核缓冲区，上层的应用程序得到writecomplete的通知，这个时候我们在发送数据。
    //可以保证所有的用户数据都发送完，writecomplatecallback回调，然后继续发送
    HighWaterMarkCallback highWaterMarkCallback_; //高水位标回调函数，在这个回调函数中就可以断开连接，避免内存不断增大导致撑爆
    CloseCallback closeCallback_;
    size_t highWaterMark_; //高水位标
    Buffer inputBuffer_;   //应用层的接收缓冲区
    Buffer outputBuffer_;  // FIXME: use list<Buffer> as output buffer.应用层的发送缓冲区，当outputbuffer高到一定程度，回调highwatermarkcallback_函数
    std::any context_;     //提供一个接口绑定一个未知类型的上下文对象，我们不清楚上层的网络程序会绑定一个什么对象，提供这样的接口，帮助应用程序
    bool reading_;
    //可变类型的解决方案有两种
    //void* 这种方法不是类型安全的
    //boost::any,好处是可以将任意类型安全存取
    //甚至在标准库容器中存放不同类型的方法，比如vector<std::any>

    // FIXME: creationTime_, lastReceiveTime_
    //        bytesReceived_, bytesSent_
};

typedef std::shared_ptr<TcpConnection> TcpConnectionPtr; //使用shared_ptr来管理tcpconnection


} // namespace net
} // namespace muduo

#endif // MUDUO_NET_TCPCONNECTION_H
