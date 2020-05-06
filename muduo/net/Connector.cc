// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Connector.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

const int Connector::kMaxRetryDelayMs;
//写的客户端
Connector::Connector(EventLoop *loop, const InetAddress &serverAddr)
    : loop_(loop),
      serverAddr_(serverAddr),
      connect_(false),
      state_(kDisconnected),
      retryDelayMs_(kInitRetryDelayMs)
{
    LOG_DEBUG << "ctor[" << this << "]";
}

Connector::~Connector()
{
    LOG_DEBUG << "dtor[" << this << "]";
    assert(!channel_);
}
//可以跨线程调用
void Connector::start()
{
    connect_ = true;                                            //如果处于连接状态
    loop_->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
}

void Connector::startInLoop()
{
    loop_->assertInLoopThread(); //断言处于io线程中
    assert(state_ == kDisconnected);
    if (connect_) //如果connect_为true，发起连接
    {
        connect();
    }
    else
    {
        LOG_DEBUG << "do not connect";
    }
}

void Connector::stop()//关闭connect
{
    connect_ = false;
    loop_->queueInLoop(std::bind(&Connector::stopInLoop, this)); // FIXME: unsafe
    // FIXME: cancel timer
}

void Connector::stopInLoop()
{
    loop_->assertInLoopThread();
    if (state_ == kConnecting)
    {
        setState(kDisconnected);
        int sockfd = removeAndResetChannel(); //将通道从poller中移除关注，并将channel置空
        retry(sockfd);                        //这里并非要重连，只是调用sockets::close(sockfd);
    }
}

void Connector::connect()
{
    int sockfd = sockets::createNonblockingOrDie(); //创建非阻塞套接字
    int ret = sockets::connect(sockfd, serverAddr_.getSockAddrInet());
    int savedErrno = (ret == 0) ? 0 : errno;
    switch (savedErrno)
    {
    case 0:
    case EINPROGRESS: //非阻塞套接字，未连接成功返回吗是einprogess表示正在连接
    case EINTR:
    case EISCONN: //连接成功
        connecting(sockfd);
        break;

    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
        retry(sockfd); //表示没有连接成功重连
        break;

    case EACCES:
    case EPERM:
    case EAFNOSUPPORT:
    case EALREADY:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
        LOG_SYSERR << "connect error in Connector::startInLoop " << savedErrno;
        sockets::close(sockfd); //要是这种错误码，不能重连，直接关闭套接字
        break;

    default:
        LOG_SYSERR << "Unexpected error in Connector::startInLoop " << savedErrno;
        sockets::close(sockfd);
        // connectErrorCallback_();
        break;
    }
}
//重启，不能跨线程调用
void Connector::restart()
{
    loop_->assertInLoopThread();
    setState(kDisconnected);
    retryDelayMs_ = kInitRetryDelayMs;//初始的重连时间设为0.5s
    connect_ = true;
    startInLoop();
}

void Connector::connecting(int sockfd)
{
    setState(kConnecting); //在这里设置状态
    assert(!channel_);
    //channel与socketfd关联
    channel_.reset(new Channel(loop_, sockfd));
    //设置可写回调函数，这时候如果socket没有错误，sockfd就处于可写状态
    channel_->setWriteCallback(
        std::bind(&Connector::handleWrite, this)); // FIXME: unsafe
    //设置错误回调函数
    channel_->setErrorCallback(
        std::bind(&Connector::handleError, this)); // FIXME: unsafe

    // channel_->tie(shared_from_this()); is not working,
    // as channel_ is not managed by shared_ptr
    channel_->enableWriting(); //让poller关注可写事件
}

int Connector::removeAndResetChannel()
{
    channel_->disableAll();
    channel_->remove(); //从poller中移除关注
    int sockfd = channel_->fd();
    // Can't reset channel_ here, because we are inside Channel::handleEvent
    //不能在这里重置channel_，因为正在调用Channel::handleEvent
    loop_->queueInLoop(std::bind(&Connector::resetChannel, this)); // FIXME: unsafe，跳出函数后才进行resetChannel
    return sockfd;
}

void Connector::resetChannel()
{
    channel_.reset(); //channel_置空
}

void Connector::handleWrite()
{
    LOG_TRACE << "Connector::handleWrite " << state_;

    if (state_ == kConnecting)
    {
        int sockfd = removeAndResetChannel(); //从poller中移除关注，并将channel置空
        //socket可写并不意味着连接一定建立成功
        //还需要使用getsocketopt(sockfd,SOL_SOCKET,SO_ERROR,...)在次确认一下
        int err = sockets::getSocketError(sockfd);
        if (err) //有错误
        {
            LOG_WARN << "Connector::handleWrite - SO_ERROR = "
                     << err << " " << strerror_tl(err);
            retry(sockfd); //重连
        }
        else if (sockets::isSelfConnect(sockfd)) //自连接
        {
            LOG_WARN << "Connector::handleWrite - Self connect";
            retry(sockfd); //重连
        }
        else //连接成功
        {
            setState(kConnected); //设置状态
            if (connect_)
            {
                newConnectionCallback_(sockfd);
            }
            else
            {
                sockets::close(sockfd);
            }
        }
    }
    else
    {
        // what happened?
        assert(state_ == kDisconnected);
    }
}

void Connector::handleError()
{
    LOG_ERROR << "Connector::handleError state=" << state_;
    if (state_ == kConnecting)
    {
        int sockfd = removeAndResetChannel(); //从poller中移除关注，并将channel置空
        int err = sockets::getSocketError(sockfd);
        LOG_TRACE << "SO_ERROR = " << err << " " << strerror_tl(err);
        retry(sockfd); //重新发起连接
    }
}
//采用back-off策略重连，即重连时间逐渐延长，0.5s，1s，2s直到30s
void Connector::retry(int sockfd)
{
    sockets::close(sockfd);  //重连之前先关闭套接字
    setState(kDisconnected); //设置套接字状态
    if (connect_)
    {
        LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort()
                 << " in " << retryDelayMs_ << " milliseconds. ";
        //注册一个定时操作，重连
        loop_->runAfter(retryDelayMs_ / 1000.0, //在这么多秒之后在次发起重连
                        std::bind(&Connector::startInLoop, shared_from_this()));
        retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);//下一次重连时间，是这一次时间的二倍，在和最大重连时间取较小值
    }
    else
    {
        LOG_DEBUG << "do not connect";
    }
}

