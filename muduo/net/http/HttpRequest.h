// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_HTTP_HTTPREQUEST_H
#define MUDUO_NET_HTTP_HTTPREQUEST_H

#include "muduo/base/copyable.h"
#include "muduo/base/Timestamp.h"
#include "muduo/base/Types.h"

#include <map>
#include <assert.h>
#include <stdio.h>

namespace muduo
{//http请求类封装
namespace net
{

class HttpRequest : public muduo::copyable
{
 public:
  enum Method
  {
    kInvalid, kGet, kPost, kHead, kPut, kDelete//请求方法
  };
  enum Version
  {
    kUnknown, kHttp10, kHttp11//http版本
  };

  HttpRequest()
    : method_(kInvalid),//构造函数初始化
      version_(kUnknown)
  {
  }

  void setVersion(Version v)
  {
    version_ = v; //设置版本
  }

  Version getVersion() const
  { return version_; }

  bool setMethod(const char* start, const char* end)
  {
    assert(method_ == kInvalid);
    string m(start, end);//根据这个字符串来判断
    if (m == "GET")
    {
      method_ = kGet;
    }
    else if (m == "POST")
    {
      method_ = kPost;
    }
    else if (m == "HEAD")
    {
      method_ = kHead;
    }
    else if (m == "PUT")
    {
      method_ = kPut;
    }
    else if (m == "DELETE")
    {
      method_ = kDelete;
    }
    else
    {
      method_ = kInvalid;
    }
    return method_ != kInvalid;//看是否请求成功
  }

  Method method() const
  { return method_; }

  const char* methodString() const //请求方法转换为字符串
  {
    const char* result = "UNKNOWN";
    switch(method_)
    {
      case kGet:
        result = "GET";
        break;
      case kPost:
        result = "POST";
        break;
      case kHead:
        result = "HEAD";
        break;
      case kPut:
        result = "PUT";
        break;
      case kDelete:
        result = "DELETE";
        break;
      default:
        break;
    }
    return result;
  }

  void setPath(const char* start, const char* end)//设置路径
  {
    path_.assign(start, end); //设置到path
  }

  const string& path() const
  { return path_; }

  void setQuery(const char* start, const char* end)
  {
    query_.assign(start, end);
  }

  const string& query() const
  { return query_; }

  void setReceiveTime(Timestamp t) //设置接收时间
  { receiveTime_ = t; }

  Timestamp receiveTime() const
  { return receiveTime_; }

    void addHeader(const char *start, const char *colon, const char *end)
    {                                    //添加一个头部信息
        string field(start, colon); //header域
        ++colon;
        //去除左空格
        while (colon < end && isspace(*colon)) //header值
        {
            ++colon;
        }
        string value(colon, end);
        //去除右空格
        while (!value.empty() && isspace(value[value.size() - 1]))
        {
            value.resize(value.size() - 1);
        }
        headers_[field] = value; //将value的值保存到headers
    }


  string getHeader(const string& field) const//根据头域返回值
  {
    string result;
    std::map<string, string>::const_iterator it = headers_.find(field);
    if (it != headers_.end())
    {
      result = it->second;
    }
    return result;
  }

  const std::map<string, string>& headers() const
  { return headers_; }

  void swap(HttpRequest& that)//交换数据成员
  {
    std::swap(method_, that.method_);
    std::swap(version_, that.version_);
    path_.swap(that.path_);
    query_.swap(that.query_);
    receiveTime_.swap(that.receiveTime_);
    headers_.swap(that.headers_);
  }

 private:
    Method method_;    //请求方法
    Version version_;  //协议版本1.0/1.1
    string path_; //请求路径
    string query_;
    Timestamp receiveTime_;                      //请求时间
    std::map<string, string> headers_; //header列表
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_HTTP_HTTPREQUEST_H
