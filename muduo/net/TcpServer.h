// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPSERVER_H
#define MUDUO_NET_TCPSERVER_H

#include "muduo/base/Atomic.h"
#include "muduo/base/Types.h"
#include "muduo/net/TcpConnection.h"

#include <map>

namespace muduo
{
namespace net
{

class Acceptor;
class EventLoop;
class EventLoopThreadPool;

///
/// TCP server, supports single-threaded and thread-pool models.
///
/// This is an interface class, so don't expose too much details.
class TcpServer : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;
  enum Option
  {
    kNoReusePort,
    kReusePort,
  };

  //TcpServer(EventLoop* loop, const InetAddress& listenAddr);
  TcpServer(EventLoop* loop,
            const InetAddress& listenAddr,
            const string& nameArg,
            Option option = kNoReusePort);
  ~TcpServer();  // force out-line dtor, for std::unique_ptr members.

  const string& ipPort() const { return ipPort_; }
  const string& name() const { return name_; }
  EventLoop* getLoop() const { return loop_; }

  /// Set the number of threads for handling input.
  ///
  /// Always accepts new connection in loop's thread.
  /// Must be called before @c start
  /// @param numThreads
  /// - 0 means all I/O in loop's thread, no thread will created.
  ///   this is the default value.
  /// - 1 means all I/O in another thread.
  /// - N means a thread pool with N threads, new connections
  ///   are assigned on a round-robin basis.
    void setThreadNum(int numThreads);                       //实际上就是调用线程池这个类的setthreadnum，设置线程池中的io线程数量，但是不包括主的io线程，比如你设置了3个，实际上加上主的io线程是四个
    void setThreadInitCallback(const ThreadInitCallback &cb) //通过这个函数设置，线程池初始化的回调函数
    {
        threadInitCallback_ = cb;
    }
    /// valid after calling start()
    std::shared_ptr<EventLoopThreadPool> threadPool()
    {
        return threadPool_;
    }

    /// Starts the server if it's not listenning.
    ///
    /// It's harmless to call it multiple times.
    /// Thread safe.
    void start(); //启动线程池
    /*******************************************************************/
    /// 用户使用了TcpServer,那么用户就必须负责给TcpServer中的这个几个变量进行赋值
    /// 这几个回调都是从用户层传递给TcpServer然后再渗透到这里的
    /// Set connection callback.
    /// Not thread safe.
    //设置连接到来或链接关闭的回调函数
    void setConnectionCallback(const ConnectionCallback &cb)
    {
        connectionCallback_ = cb;
    }

   /// Set message callback.
    /// Not thread safe.
    //设置消息回调函数
    void setMessageCallback(const MessageCallback &cb)
    {
        messageCallback_ = cb;
    }

    /// Set write complete callback.
    /// Not thread safe.
    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    {
        writeCompleteCallback_ = cb;
    }
    /***************************************************************/
private:
    /// Not thread safe, but in loop
    void newConnection(int sockfd, const InetAddress &peerAddr);
    /// Thread safe.
    void removeConnection(const TcpConnectionPtr &conn);
    /// Not thread safe, but in loop
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    typedef std::map<string, TcpConnectionPtr> ConnectionMap; //连接列表是一个map容器key是链接名称，value保存的变量就是TcpConnection的指针

    EventLoop *loop_;                    // the acceptor loop
    const string hostport_;              //服务端口
    const string name_;                  //服务名
    std::unique_ptr<Acceptor> acceptor_; // avoid revealing Acceptor，Acceptor负责了一个socketfd,这个socketfd就是一个监听套接字。类是属于内部类
    std::shared_ptr<EventLoopThreadPool> threadPool_;
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_; //数据发送完毕，会调用此函数，tcpconnection中的回调函数在这里调用
    ThreadInitCallback threadInitCallback_;       //io线程池中的线程在进入事件循环前，会调用此函数
    AtomicInt32 started_;                         //是否启动
    // always in loop thread
    int nextConnId_;            //下一个链接id
    ConnectionMap connections_; //连接列表,保留着在这个服务器上的所有连接
};


}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPSERVER_H
