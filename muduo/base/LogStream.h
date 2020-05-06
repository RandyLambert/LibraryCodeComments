// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGSTREAM_H
#define MUDUO_BASE_LOGSTREAM_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/StringPiece.h"
#include "muduo/base/Types.h"
#include <assert.h>
#include <string.h> // memcpy

namespace muduo
{

namespace detail
{

const int kSmallBuffer = 4000;
const int kLargeBuffer = 4000 * 1000;
//LogStream主要用来格式化输出，重载了<<运算符，同时也有自己的一块缓冲区，这里缓冲区的存在是为了缓存一行，把多个<<的结果连成一块
template <int SIZE> //非类型参数，不是传递类型而是传递值
class FixedBuffer
{
public:
    FixedBuffer(const FixedBuffer &) = delete;
    FixedBuffer &operator=(const FixedBuffer &) = delete;
    FixedBuffer()
        : cur_(data_)
    {
        setCookie(cookieStart); //穿进去是还没有使用，暂时没用
    }

    ~FixedBuffer()
    {
        setCookie(cookieEnd);
    }

    void append(const char * /*restrict*/ buf, size_t len)
    {
        if (static_cast<size_t>(avail()) > len) //当前可用的空间大于len，则就可以将其添加进去
        {
            memcpy(cur_, buf, len);
            cur_ += len;
        }
        else
        { //如果小于，只要缓冲区不等于0，则将部分大小放入缓冲区，但是最后一位不能占，因为可能会放结束符'\0'
            if (static_cast<size_t>(avail()) > 0)
            {
                memcpy(cur_, buf, static_cast<size_t>(avail()) - 1);
                cur_ += (static_cast<size_t>(avail()) - 1);
            }
        }
    }

    const char *data() const { return data_; } //首地址，current-data是缓冲区所有的数据
    int length() const { return static_cast<int>(cur_ - data_); }

    // write to data_ directly
    char *current() { return cur_; } //目前缓冲区使用的位置，end-current是当前可用空间
    int avail() const { return static_cast<int>(end() - cur_); }
    void add(size_t len) { cur_ += len; }

    void reset() { cur_ = data_; }                 //重置，只需要把指针移到开头，不需要清零
    void bzero() { ::bzero(data_, sizeof data_); } //清零

    // for used by GDB
    const char *debugString();                             //给cur位置加上'\0'，相当于是加结束符，变成字符串
    void setCookie(void (*cookie)()) { cookie_ = cookie; } //函数就是将函数指针指向传递进去的指针
    // for used by unit test
    std::string asString() const { return std::string(data_, length()); }

private:
    const char *end() const { return data_ + sizeof data_; } //整个缓冲区size位的下一位
    // Must be outline function for cookies.
    static void cookieStart();
    static void cookieEnd();

    void (*cookie_)(); //函数指针
    char data_[SIZE];  //缓冲区大小通过模板传递
    char *cur_;        //指向缓冲区最后一个位置的下一个
};

} // namespace detail

class LogStream
{
    typedef LogStream self;

public:
    typedef detail::FixedBuffer<detail::kSmallBuffer> Buffer;
    LogStream(const LogStream &) = delete;
    LogStream &operator=(const LogStream &) = delete;

    self &operator<<(bool v) //bool类型是真存1假存0
    {
        buffer_.append(v ? "1" : "0", 1);
        return *this;
    }

    self &operator<<(short); //处理所有类型
    self &operator<<(unsigned short);
    self &operator<<(int);
    self &operator<<(unsigned int);
    self &operator<<(long);
    self &operator<<(unsigned long);
    self &operator<<(long long);
    self &operator<<(unsigned long long);

    self &operator<<(const void *);

    self &operator<<(float v)
    {
        *this << static_cast<double>(v);
        return *this;
    }
    self &operator<<(double);
    // self& operator<<(long double);

    self &operator<<(char v)
    {
        buffer_.append(&v, 1);
        return *this;
    }

    // self& operator<<(signed char);
    // self& operator<<(unsigned char);

    self &operator<<(const char *str)
    {
        if (str)
        {
            buffer_.append(str, strlen(str));
        }
        else
        {
            buffer_.append("(null)", 6);
        }
        return *this;
    }

    self &operator<<(const unsigned char *str)
    {
        return operator<<(reinterpret_cast<const char *>(str));
    }

    self &operator<<(const std::string &v)
    {
        buffer_.append(v.c_str(), v.size());
        return *this;
    }

    self &operator<<(const StringPiece &v)
    {
        buffer_.append(v.data(), v.size());
        return *this;
    }

    void append(const char *data, int len) { buffer_.append(data, len); }
    const Buffer &buffer() const { return buffer_; }
    void resetBuffer() { buffer_.reset(); }

private:
    void staticCheck();

    template <typename T>
    void formatInteger(T); //成员模板

    Buffer buffer_;

    static const int kMaxNumericSize = 32;
};

class Fmt
{
public:
    template <typename T>        //成员模板
    Fmt(const char *fmt, T val); //把整数val，按照fmt的格式进行格式化到buf中

    const char *data() const { return buf_; }
    int length() const { return length_; }

private:
    char buf_[32];
    int length_;
};

inline LogStream &operator<<(LogStream &s, const Fmt &fmt)
{
    s.append(fmt.data(), fmt.length());
    return s;
}

// Format quantity n in SI units (k, M, G, T, P, E).
// The returned string is atmost 5 characters long.
// Requires n >= 0
string formatSI(int64_t n);

// Format quantity n in IEC (binary) units (Ki, Mi, Gi, Ti, Pi, Ei).
// The returned string is atmost 6 characters long.
// Requires n >= 0
string formatIEC(int64_t n);

} // namespace muduo

#endif // MUDUO_BASE_LOGSTREAM_H
