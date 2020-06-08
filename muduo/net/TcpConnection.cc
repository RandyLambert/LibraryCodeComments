// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpConnection.h"

#include "muduo/base/Logging.h"
#include "muduo/base/WeakCallback.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Socket.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

void muduo::net::defaultConnectionCallback(const TcpConnectionPtr &conn)
{
    //默认的连接到来函数，如果自己设置，是在tcpserver设置
    LOG_TRACE << conn->localAddress().toIpPort() << " -> "
              << conn->peerAddress().toIpPort() << " is "
              << (conn->connected() ? "UP" : "DOWN");
    // do not call conn->forceClose(), because some users want to register message callback only.
}

void muduo::net::defaultMessageCallback(const TcpConnectionPtr &,
                                        Buffer *buf,
                                        Timestamp)
{ //消息到来函数，如果自己设置，是在tcpserver处设置
    buf->retrieveAll();
}

TcpConnection::TcpConnection(EventLoop *loop,
                             const string &nameArg,
                             int sockfd,
                             const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CHECK_NOTNULL(loop)),
      name_(nameArg),
      state_(kConnecting),
      reading_(true),
      socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      highWaterMark_(64 * 1024 * 1024)
{ //在这些函数中调用了从用户层传递给TcpServer并且渗透到TcpConnection中的messageCallback_ writeCompleteCallback_函数
    //通道可读时间到来的时候，回到tcpconnection::handleread，-1是时间发生时间
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    //通道可写事件到来的时候，回调tcpconnection::handlewrite
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this));
    //连接关闭，回调tcpconnection::handleclose
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this));
    //发生错误，回调tcpconnection::handleerror
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this));
    LOG_DEBUG << "TcpConnection::ctor[" << name_ << "] at " << this
              << " fd=" << sockfd;
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_DEBUG << "TcpConnection::dtor[" << name_ << "] at " << this
              << " fd=" << channel_->fd()
              << " state=" << stateToString();
    assert(state_ == kDisconnected);
}

bool TcpConnection::getTcpInfo(struct tcp_info *tcpi) const
{
    return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
    char buf[1024];
    buf[0] = '\0';
    socket_->getTcpInfoString(buf, sizeof buf);
    return buf;
}

//线程安全的，可以跨线程调用
void TcpConnection::send(const void *data, int len)
{
    send(StringPiece(static_cast<const char *>(data), len));
}
//线程安全的，可以跨线程调用
void TcpConnection::send(const StringPiece &message)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread()) //如果是当前io线程调用
        {
            sendInLoop(message);
        }
        else
        {
            void (TcpConnection::*fp)(const StringPiece &message) = &TcpConnection::sendInLoop;
            loop_->runInLoop( //直接转到eventloop所属线程调用
                std::bind(fp,
                          this, // FIXME
                          message.as_string()));
        }
    }
}

void TcpConnection::send(string &&message)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread()) //如果是当前io线程调用
        {
            sendInLoop(message);
        }
        else
        {
            void (TcpConnection::*fp)(const StringPiece &message) = &TcpConnection::sendInLoop;
            loop_->runInLoop( //直接转到eventloop所属线程调用
                std::bind(fp,
                          this,
                          std::forward<string>(message)));
        }
    }
}

// FIXME efficiency!!!线程安全的，可以跨线程调用
void TcpConnection::send(Buffer *buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf->peek(), buf->readableBytes());
            buf->retrieveAll(); //把缓冲区数据移除
        }
        else
        {
            void (TcpConnection::*fp)(const StringPiece &message) = &TcpConnection::sendInLoop;
            loop_->runInLoop(
                std::bind(fp,
                          this, // FIXME
                          buf->retrieveAllAsString()));
        }
    }
}

//线程安全的，可以跨线程调用
void TcpConnection::send(Buffer &&buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.peek(), buf.readableBytes());
            buf.retrieveAll(); //把缓冲区数据移除
        }
        else
        {
            void (TcpConnection::*fp)(const StringPiece &message) = &TcpConnection::sendInLoop;
            loop_->runInLoop(
                std::bind(fp,
                          this,
                          buf.retrieveAllAsString()));
        }
    }
}

void TcpConnection::sendInLoop(const StringPiece &message)
{
    sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void *data, size_t len)
{
    loop_->assertInLoopThread();
    ssize_t nwrote = 0;
    size_t remaining = len; //len是我们要发送的数据
    bool faultError = false;
    if (state_ == kDisconnected)
    {
        LOG_WARN << "disconnected, give up writing";
        return;
    }
    // if no thing in output queue, try writing directly
    //通道没有关注可写时间不亲个发送缓冲区没有数据，直接write
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) //没有关注可写事件，且outputbuffer缓冲区没有数据
    {
        nwrote = sockets::write(channel_->fd(), data, len); //可以直接write
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            //写完了，回调writecompletecallback
            if (remaining == 0 && writeCompleteCallback_) //如果等于0，说明都发送完毕，都拷贝到了内核缓冲区
            {
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this())); //回调writeCompareCallback
            }
        }
        else // nwrote < 0，出错了
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_SYSERR << "TcpConnection::sendInLoop";
                if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
                {
                    faultError = true;
                }
            }
        }
    }

    assert(remaining <= len);
    //没有错误，并且还有未写完的数据（说明内核发送缓冲区满，要将未写完的数据添加到output buffer中）
    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes(); //oldlen是目前outputbuffer中的数据量
        //如果超过highwatermark_（高水位标），回调highwatermarkcallback
        if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_) //highwatermark肯定要小于oldlen长度
        {
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining)); //回调highwatermarkcallback，回调中可能把这个连接断开
        }
        outputBuffer_.append(static_cast<const char *>(data) + nwrote, remaining); //然后后面还有(data) + nwrote的数据没发送，就把他添加到outputbuffer中
        if (!channel_->isWriting())                                                //outputbuffer中有数据了，如果现在还没有关注pollout事件，则现在关注这个pollout事件
        {
            channel_->enableWriting(); //关注这个pollout事件,当对等方的接受了数据，tcp的滑动窗口滑动了，这时候内核的发送缓冲区有位置了，pullout事件被触发，会回调tcpconnection::handlewrite
        }
    }
}

void TcpConnection::shutdown()
{ //应用程序想关闭连接，但是有可能正处于发送数据的过程中，output buffer中有数据还没发送完，不能调用close()
    //保证conn->send(buff);只要网络没有故障，保证必须发到对端
    //conn->shutdown();如果想要关闭，必须判断是否有没法删的数据，如果有不应该直接关闭

    //不可以跨线程调用
    // FIXME: use compare and swap调用原子操作
    if (state_ == kConnected)
    {
        setState(kDisconnecting);                                                        //如果还处于pollerout状态，只是将状态改为了kdisconnecting，并没有关闭连接
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, shared_from_this())); //调用shutdownInLoop
    }
}

void TcpConnection::shutdownInLoop()
{
    loop_->assertInLoopThread(); //断言在io线程调用
    if (!channel_->isWriting())  //如果不在处于pollout状态
    {
        // we are not writing
        socket_->shutdownWrite(); //关闭写的一端，否则不能关闭
        //当数据发完之后，服务端还要在判断一下现在状态是否处于kdisconnecting状态
        //如果处于这个状态，服务端主动断开和客户端的连接
        //这就意味着 客户端 read返回为0 close(conn);
        //服务端这时会收到两个事件，POLLHUP|POLLIN，在处理
    }
}

// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(std::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

void TcpConnection::forceClose()
{
    // FIXME: use compare and swap
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        setState(kDisconnecting);
        loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        setState(kDisconnecting);
        loop_->runAfter(
            seconds,
            makeWeakCallback(shared_from_this(),
                             &TcpConnection::forceClose)); // not forceCloseInLoop to avoid race condition
    }
}

void TcpConnection::forceCloseInLoop()
{
    loop_->assertInLoopThread();
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        // as if we received 0 byte in handleRead();
        handleClose();
    }
}

const char *TcpConnection::stateToString() const
{
    switch (state_)
    {
    case kDisconnected:
        return "kDisconnected";
    case kConnecting:
        return "kConnecting";
    case kConnected:
        return "kConnected";
    case kDisconnecting:
        return "kDisconnecting";
    default:
        return "unknown state";
    }
}

void TcpConnection::setTcpNoDelay(bool on)
{
    socket_->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
    loop_->runInLoop(std::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::startReadInLoop()
{
    loop_->assertInLoopThread();
    if (!reading_ || !channel_->isReading())
    {
        channel_->enableReading();
        reading_ = true;
    }
}

void TcpConnection::stopRead()
{
    loop_->runInLoop(std::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::stopReadInLoop()
{
    loop_->assertInLoopThread();
    if (reading_ || channel_->isReading())
    {
        channel_->disableReading();
        reading_ = false;
    }
}

void TcpConnection::connectEstablished()
{
    loop_->assertInLoopThread();
    assert(state_ == kConnecting);
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading(); //tcpconnection所对应的通道加入到poller关注

    connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    loop_->assertInLoopThread();
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll();

        connectionCallback_(shared_from_this());
    }
    channel_->remove();
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    loop_->assertInLoopThread();
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno); //当消息到来时读这个通道
    if (n > 0)
    {
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0)
    {
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_SYSERR << "TcpConnection::handleRead";
        handleError();
    }
}
//内核缓冲区有空间了，回调该函数
void TcpConnection::handleWrite() //pollout事件触发了
{
    loop_->assertInLoopThread();
    if (channel_->isWriting()) //如果关注了pollout事件
    {
        ssize_t n = sockets::write(channel_->fd(), //这时就把outputbuffer中的写入
                                   outputBuffer_.peek(),
                                   outputBuffer_.readableBytes());
        if (n > 0) //不一定能写完，写了n个字节
        {
            outputBuffer_.retrieve(n);              //缓冲区下标的移动，因为这时已经写了n个字节了
            if (outputBuffer_.readableBytes() == 0) //==0说明发送缓冲区已清空
            {
                channel_->disableWriting(); //停止关注pollout事件，以免出现busy_loop
                if (writeCompleteCallback_) //回调writecomplatecallback
                {
                    //应用层发送缓冲区被清空，就回调writecomplatecallback
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting) //发送缓冲区已清空并且连接状态是kdisconnecting,要关闭连接(在shutdown函数哪里，如果要关闭连接，关闭前必须把数据都发送到对端)
                {
                    shutdownInLoop(); //关闭连接
                }
            }
        }
        else
        {
            LOG_SYSERR << "TcpConnection::handleWrite"; //发生错误
        }
    }
    else
    {
        LOG_TRACE << "Connection fd = " << channel_->fd()
                  << " is down, no more writing";
    }
}

void TcpConnection::handleClose()
{
    loop_->assertInLoopThread();
    LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
    assert(state_ == kConnected || state_ == kDisconnecting);
    // we don't close fd, leave it to dtor, so we can find leaks easily.
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr guardThis(shared_from_this());
    connectionCallback_(guardThis); //这一行可以不调用，这里调用的是用户的回调函数onconnection函数，处理三个半事件的函数，里面判断是连接还是断开
    // must be the last line
    closeCallback_(guardThis); //调用tcpserverremoveconnection
}

void TcpConnection::handleError()
{
    int err = sockets::getSocketError(channel_->fd());
    LOG_ERROR << "TcpConnection::handleError [" << name_
              << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}
