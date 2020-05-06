// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_EXCEPTION_H
#define MUDUO_BASE_EXCEPTION_H

#include "muduo/base/Types.h"
#include <exception>

namespace muduo
{

class Exception : public std::exception //继承标准库的异常类
{
public:
    explicit Exception(const char *what);
    explicit Exception(const string &what);
    virtual ~Exception() noexcept;
    virtual const char *what() const noexcept;
    const char *stackTrace() const noexcept;

private:
    void fillStackTrace(); //主要
    string demangle(const char *symbol);
    string message_; //存错误抛出的内容
    string stack_;   //存调用的函数栈
};

}  // namespace muduo

#endif  // MUDUO_BASE_EXCEPTION_H
