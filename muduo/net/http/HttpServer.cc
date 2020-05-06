// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/http/HttpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/http/HttpContext.h"
#include "muduo/net/http/HttpRequest.h"
#include "muduo/net/http/HttpResponse.h"

using namespace muduo;
using namespace muduo::net;

namespace muduo
{
namespace net
{
namespace detail
{

void defaultHttpCallback(const HttpRequest &, HttpResponse *resp) //如果没有设置http回调函数的默认回调函数
{
    resp->setStatusCode(HttpResponse::k404NotFound);
    resp->setStatusMessage("Not Found");
    resp->setCloseConnection(true);
}

} // namespace detail
} // namespace net
} // namespace muduo

HttpServer::HttpServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &name,
                       TcpServer::Option option)
    : server_(loop, listenAddr, name, option),
      httpCallback_(detail::defaultHttpCallback)
{
    server_.setConnectionCallback( //注册这两个回调函数
        std::bind(&HttpServer::onConnection, this, _1));
    server_.setMessageCallback(
        std::bind(&HttpServer::onMessage, this, _1, _2, _3));
}

void HttpServer::start()
{
    LOG_WARN << "HttpServer[" << server_.name()
             << "] starts listenning on " << server_.ipPort();
    server_.start();
}

void HttpServer::onConnection(const TcpConnectionPtr &conn)
{
    if (conn->connected())
    {
        conn->setContext(HttpContext()); //tcpconnection与一个httpcontext绑定
    }
}

void HttpServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buf,
                           Timestamp receiveTime)
{
    HttpContext *context = std::any_cast<HttpContext>(conn->getMutableContext()); //获取的是可以改变的

    if (!context->parseRequest(buf, receiveTime)) //获取请求包，更好的做法是让parserequest作为httpcontext的成员函数
    {
        conn->send("HTTP/1.1 400 Bad Request\r\n\r\n"); //请求失败
        conn->shutdown();
    }
    //请求消息解析完毕
    if (context->gotAll())
    {
        onRequest(conn, context->request()); //连接和请求对象传过来
        context->reset();                    //本次请求处理完毕，重置httpcontext，适用于长连接
    }
}

void HttpServer::onRequest(const TcpConnectionPtr &conn, const HttpRequest &req)
{
    const string &connection = req.getHeader("Connection");                                //把头部取出来
    bool close = connection == "close" ||                                                  //如果等于close
                 (req.getVersion() == HttpRequest::kHttp10 && connection != "Keep-Alive"); //或者是1.0且connection不等与keep-alive，http1.0不支持长连接
    HttpResponse response(close);                                                          //处理完请求是否要关闭连接
    httpCallback_(req, &response);                                                         //回调用户的函数对http请求进行相应处理，一旦处理完了返回response对象，是一个输入输出参数
    Buffer buf;
    response.appendToBuffer(&buf);  //将对象转换为一个字符串转换到buf中
    conn->send(&buf);               //将缓冲区发送个客户端
    if (response.closeConnection()) //如果需要关闭，短连接
    {
        conn->shutdown(); //关闭
    }
}
