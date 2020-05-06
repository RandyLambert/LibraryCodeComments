// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CHANNEL_H
#define MUDUO_NET_CHANNEL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/Timestamp.h"

#include <functional>
#include <memory>

namespace muduo
{
namespace net
{

class EventLoop;

///
/// A selectable I/O channel.
///
/// 这个类不拥有文件描述符
/// 文件描述符被socket类拥有
/// 文件描述符可能在an eventfd, a timerfd, or a signalfd

class Channel : noncopyable
{
public:
    typedef std::function<void()> EventCallback;
    typedef std::function<void(Timestamp)> ReadEventCallback; //读事件还得多一个时间戳

    Channel(EventLoop *loop, int fd); //一个eventloop可能包含多个channel，但是一个channel只能在一个eventloop
    ~Channel();

    void handleEvent(Timestamp receiveTime); //重点
    void setReadCallback(ReadEventCallback cb)
    {
        readCallback_ = std::move(cb);
    }
    void setWriteCallback(EventCallback cb)
    {
        writeCallback_ = std::move(cb);
    }
    void setCloseCallback(EventCallback cb)
    {
        closeCallback_ = std::move(cb);
    }
    void setErrorCallback(EventCallback cb)
    {
        errorCallback_ = std::move(cb);
    }

    /// Tie this channel to the owner object managed by shared_ptr,
    /// prevent the owner object being destroyed in handleEvent.
    void tie(const std::shared_ptr<void> &); //和tcpconnection对象有关系，防止对象销毁

    int fd() const { return fd_; }                  //channel对应的文件描述符
    int events() const { return events_; }          //channel注册了那些时间保存在events中
    void set_revents(int revt) { revents_ = revt; } // used by pollers
    // int revents() const { return revents_; }
    bool isNoneEvent() const { return events_ == kNoneEvent; } //判断是否没有事件

    void enableReading() //关注读事件，或者加入这个事件
    {
        events_ |= kReadEvent; //Acceptor中的listen()函数中调用了Channel中的enableReding（）函数
        //在TcpConnection中的connectEstablished()函数也调用了这个函数,connectEstablished在创建一
        //个新的连接的时候，也就是TcpServer::newConnection中被调用的
        update(); //相当与关注他的可读事件，将可读事件注册到eventloop，从而注册到eventloop的poller对象当中
    }
    void disableReading()
    {
        events_ &= ~kReadEvent;
        update();
    }
    void enableWriting()
    {
        events_ |= kWriteEvent;
        update();
    }
    void disableWriting()
    {
        events_ &= ~kWriteEvent;
        update();
    }
    void disableAll() //不关注事件了
    {
        events_ = kNoneEvent;
    }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }
    // for Poller
    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // for debug
    std::string reventsToString() const;

    void doNotLogHup() { logHup_ = false; }

    EventLoop *ownerLoop() { return loop_; }
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;  //没有关注事件
    static const int kReadEvent;  //POLLIN | POLLPRI（紧急事件），默认LT
    static const int kWriteEvent; //POLLOUT写

    EventLoop *loop_; //记录所属eventloop
    const int fd_;    //文件描述符，但是不负责关闭该文件描述符
    int events_;      //关注的事件
    int revents_;     //epoll or poll实际返回的事件
    int index_;       //used by Poller.表示在poll的事件数组中的序号，在epoll中表示的是通道的状态
    bool logHup_;     //for POLLHUP

    std::weak_ptr<void> tie_;
    bool tied_;
    bool eventHandling_; //是否在处理事件中
    bool addedToLoop_;
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

} // namespace net
} // namespace muduo

#endif // MUDUO_NET_CHANNEL_H
