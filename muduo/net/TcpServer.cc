// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Acceptor.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThreadPool.h"
#include "muduo/net/SocketsOps.h"

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)),
    ipPort_(listenAddr.toIpPort()),
    name_(nameArg),
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
    threadPool_(new EventLoopThreadPool(loop, name_)),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    nextConnId_(1)
{
    //accepter::handleread函数中会调用tcpserver::newconnection
    //_1对应的是socket文件描述符，_2对应等待是对等方的地址（inetaddress）
    acceptor_->setNewConnectionCallback(
        std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
    //直接将newConnection绑定到acceptor_的newconnectionCallback
}

TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (auto& item : connections_)
  {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

//该函数多次调用是无害的
//该函数可以跨线程调用
void TcpServer::start() //这个函数就使得Acceptor处于监听状态
{
    if (started_.getAndSet(1) == 0)
    {                                            //因为他咋判断没有启动后才会调用
        threadPool_->start(threadInitCallback_); //传递了一个线程初始化的回调函数

        assert(!acceptor_->listenning()); //断言是否处于监听状态（判断Accept是否调用了listen）
        loop_->runInLoop(
            std::bind(&Acceptor::listen, get_pointer(acceptor_))); //get_pointer可以返回智能指针的原生指针
    }
}
//创建一个tcpconnection对象
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    loop_->assertInLoopThread(); //断言在io线程
    //按照轮叫的方式选择一个eventloop，将这个新的连接交付给这个EventLoop
    EventLoop *ioLoop = threadPool_->getNextLoop(); //选出来了那个io线程
    char buf[32];                                   //缓冲区
    snprintf(buf, sizeof buf, ":%s#%d", hostport_.c_str(), nextConnId_);
    ++nextConnId_;
    string connName = name_ + buf; //表示当前连接的名称

    LOG_INFO << "TcpServer::newConnection [" << name_
             << "] - new connection [" << connName
             << "] from " << peerAddr.toIpPort();
    InetAddress localAddr(sockets::getLocalAddr(sockfd));
    // FIXME poll with zero timeout to double confirm the new connection
    // FIXME use make_shared if necessary
    //创建了一个Tcpconnection对象
    TcpConnectionPtr conn(new TcpConnection(ioLoop, //所属的loop
                                            connName,
                                            sockfd,
                                            localAddr,
                                            peerAddr));
    connections_[connName] = conn;
    conn->setConnectionCallback(connectionCallback_); //设置回调函数
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)); // FIXME: unsafe
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));    //转到ioloop所属的线程调用他进行连接
}


void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
}

