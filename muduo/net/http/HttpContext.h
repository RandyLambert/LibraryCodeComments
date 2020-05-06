// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_HTTP_HTTPCONTEXT_H
#define MUDUO_NET_HTTP_HTTPCONTEXT_H

#include "muduo/base/copyable.h"

#include "muduo/net/http/HttpRequest.h"

namespace muduo
{
namespace net
{

class Buffer;

class HttpContext : public muduo::copyable
{ //http协议解析类的封装
public:
    enum HttpRequestParseState
    {
        kExpectRequestLine, //正处于解析请求行的状态
        kExpectHeaders,     //正处于解析头部状态
        kExpectBody,        //正处于解析实体状态
        kGotAll,            //全部解析完毕
    };

    HttpContext()
        : state_(kExpectRequestLine)
    {
    }

    // default copy-ctor, dtor and assignment are fine

    // return false if any error
    bool parseRequest(Buffer *buf, Timestamp receiveTime);

    bool gotAll() const
    {
        return state_ == kGotAll;
    }
    //重置httpcontext状态
    void reset()
    {
        state_ = kExpectRequestLine; //重置为初始状态
        HttpRequest dummy;
        request_.swap(dummy); //将当前对象置空
    }

    const HttpRequest &request() const
    {
        return request_;
    }

    HttpRequest &request()//返回请求
    {
        return request_;
    }

private:
    bool processRequestLine(const char *begin, const char *end);

    HttpRequestParseState state_; //请求解析状态
    HttpRequest request_;         //http请求
};

} // namespace net
} // namespace muduo

#endif // MUDUO_NET_HTTP_HTTPCONTEXT_H
