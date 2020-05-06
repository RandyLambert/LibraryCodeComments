// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_HTTP_HTTPSERVER_H
#define MUDUO_NET_HTTP_HTTPSERVER_H

#include "muduo/net/TcpServer.h"

namespace muduo
{
namespace net
{

class HttpRequest;
class HttpResponse;

/// A simple embeddable HTTP server designed for report status of a program.
/// It is not a fully HTTP 1.1 compliant server, but provides minimum features
/// that can communicate with HttpClient and Web browser.
/// It is synchronous, just like Java Servlet.
class HttpServer : noncopyable
{
    { //http服务器类的封装
    public:
        typedef std::function<void(const HttpRequest &,
                                   HttpResponse *)> HttpCallback;
        HttpServer(EventLoop * loop,
                   const InetAddress &listenAddr,
                   const string &name,
                   TcpServer::Option option = TcpServer::kNoReusePort);

        ~HttpServer(); // force out-line dtor, for scoped_ptr members.

        EventLoop *getLoop() const { return server_.getLoop(); }

        /// Not thread safe, callback be registered before calling start().
        void setHttpCallback(const HttpCallback &cb)
        {
            httpCallback_ = cb;
        }

        void setThreadNum(int numThreads) //支持多线程
        {
            server_.setThreadNum(numThreads);
        }

        void start();

    private:
        void onConnection(const TcpConnectionPtr &conn);
        void onMessage(const TcpConnectionPtr &conn,
                       Buffer *buf,                                    //当服务器端收到了一个客户端发过来的http请求
                       Timestamp receiveTime);                         //首先回调onmessage，在onmessage中调用了onrequest，
        void onRequest(const TcpConnectionPtr &, const HttpRequest &); //在onRequest中调用了httpcallback_

        TcpServer server_;
        HttpCallback httpCallback_; //在处理http请求的时候(即调用onrequest)的过程中回调此函数，对请求进行具体的处理
    };

} // namespace net
} // namespace net

#endif // MUDUO_NET_HTTP_HTTPSERVER_H
