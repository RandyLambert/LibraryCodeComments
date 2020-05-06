// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Buffer.h"
#include "muduo/net/http/HttpContext.h"

using namespace muduo;
using namespace muduo::net;
//http服务器类的封装
bool HttpContext::processRequestLine(const char* begin, const char* end)
{
  bool succeed = false;
  const char* start = begin;
  const char* space = std::find(start, end, ' ');//根据协议格式，先查找空格所在位置
  if (space != end && request_.setMethod(start, space))//解析请求方法
  {
    start = space+1;
    space = std::find(start, end, ' ');
    if (space != end)
    {
      const char* question = std::find(start, space, '?');
      if (question != space)
      {
        request_.setPath(start, question);
        request_.setQuery(question, space);
      }
      else
      {
        request_.setPath(start, space);//解析path
      }
      start = space+1;
      succeed = end-start == 8 && std::equal(start, end-1, "HTTP/1.");
      if (succeed)
      {
        if (*(end-1) == '1')
        {
          request_.setVersion(HttpRequest::kHttp11);//判断是http1.1
        }
        else if (*(end-1) == '0')
        {
          request_.setVersion(HttpRequest::kHttp10);//判断是http1.0
        }
        else
        {
          succeed = false;
        }
      }
    }
  }
  return succeed;
}

// return false if any error
bool HttpContext::parseRequest(Buffer* buf, Timestamp receiveTime)
{
  bool ok = true;
  bool hasMore = true;
  while (hasMore) //相当与一个状态机
  {
    if (state_ == kExpectRequestLine)//处于解析请求行状态
    {
      const char* crlf = buf->findCRLF();//这些数据都保存到缓冲区当中，在缓冲区寻找\r\n，头部每一行都有一个\r\n
      if (crlf)
      {
        ok = processRequestLine(buf->peek(), crlf);//解析请求行
        if (ok)
        {
context->request().setReceiveTime(receiveTime); //设置请求时间
                    buf->retrieveUntil(crlf + 2);                   //将请求行从buf中取回，包括\r\n，所以要+2
                    context->receiveRequestLine();                  //httpcontext将状态改为kexpectheaders
        }
        else
        {
          hasMore = false;
        }
      }
      else
      {
        hasMore = false;
      }
    }
    else if (state_ == kExpectHeaders)//处于解析header的状态
    {
      const char* crlf = buf->findCRLF();
      if (crlf)
      {
        const char* colon = std::find(buf->peek(), crlf, ':');//查找冒号所在位置
        if (colon != crlf)
        {
          request_.addHeader(buf->peek(), colon, crlf);
        }
        else
        {
          // empty line, end of header
          // FIXME:
          state_ = kGotAll;//httpcontext将状态改为kgotall
          hasMore = false;
        }
        buf->retrieveUntil(crlf + 2);//将header从buf中取回，包括\r\n
      }
      else
      {
        hasMore = false;
      }
    }
    else if (state_ == kExpectBody) //当前还暂时不支持带body，需要补充
    {
      // FIXME:
    }
  }
  return ok;
}
