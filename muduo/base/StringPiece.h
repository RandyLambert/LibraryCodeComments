// Taken from PCRE pcre_stringpiece.h
//
// Copyright (c) 2005, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Sanjay Ghemawat
//
// A string like object that points into another piece of memory.
// Useful for providing an interface that allows clients to easily
// pass in either a "const char*" or a "string".
//
// Arghh!  I wish C++ literals were automatically of type "string".

#ifndef MUDUO_BASE_STRINGPIECE_H
#define MUDUO_BASE_STRINGPIECE_H

#include <string.h>
#include <iosfwd> // for ostream forward-declaration

#include "muduo/base/Types.h"

namespace muduo
{

// For passing C-style string argument to a function.
class StringArg // copyable
{
public:
    StringArg(const char *str)
        : str_(str)
    {
    }

    StringArg(const string &str)
        : str_(str.c_str())
    {
    }

    const char *c_str() const { return str_; }

private:
    const char *str_;
};

class StringPiece
{
private:
    const char *ptr_; //一个指针
    int length_;      //一个长度

public:
    // We provide non-explicit singleton constructors so users can pass
    // in a "const char*" or a "string" wherever a "StringPiece" is
    // expected.
    StringPiece()
        : ptr_(NULL), length_(0) {}
    StringPiece(const char *str) //初始化
        : ptr_(str), length_(static_cast<int>(strlen(ptr_)))
    {
    }
    StringPiece(const unsigned char *str)
        : ptr_(reinterpret_cast<const char *>(str)),
          length_(static_cast<int>(strlen(ptr_))) {}
    StringPiece(const std::string &str)
        : ptr_(str.data()), length_(static_cast<int>(str.size())) {}
    StringPiece(const char *offset, int len)
        : ptr_(offset), length_(len) {}

    // data() may return a pointer to a buffer with embedded NULs, and the
    // returned buffer may or may not be null terminated.  Therefore it is
    // typically a mistake to pass data() to a routine that expects a NUL
    // terminated string.  Use "as_string().c_str()" if you really need to do
    // this.  Or better yet, change your routine so it does not rely on NUL
    // termination.
    const char *data() const { return ptr_; }
    int size() const { return length_; }
    bool empty() const { return length_ == 0; }
    const char *begin() const { return ptr_; }
    const char *end() const { return ptr_ + length_; }

    void clear()
    {
        ptr_ = NULL;
        length_ = 0;
    }
    void set(const char *buffer, int len)
    {
        ptr_ = buffer;
        length_ = len;
    }
    void set(const char *str)
    {
        ptr_ = str;
        length_ = static_cast<int>(strlen(str));
    }
    void set(const void *buffer, int len)
    {
        ptr_ = reinterpret_cast<const char *>(buffer);
        length_ = len;
    }

    char operator[](int i) const { return ptr_[i]; }

    void remove_prefix(int n) //去除前缀
    {
        ptr_ += n;
        length_ -= n;
    }

    void remove_suffix(int n) //去除后缀
    {
        length_ -= n;
    }

    bool operator==(const StringPiece &x) const
    {
        return ((length_ == x.length_) &&
                (memcmp(ptr_, x.ptr_, length_) == 0));
    }
    bool operator!=(const StringPiece &x) const
    {
        return !(*this == x);
    }

#define STRINGPIECE_BINARY_PREDICATE(cmp, auxcmp)                                \
    bool operator cmp(const StringPiece &x) const                                \
    {                                                                            \
        int r = memcmp(ptr_, x.ptr_, length_ < x.length_ ? length_ : x.length_); \
        return ((r auxcmp 0) || ((r == 0) && (length_ cmp x.length_)));          \
    }
    STRINGPIECE_BINARY_PREDICATE(<, <);
    STRINGPIECE_BINARY_PREDICATE(<=, <);
    STRINGPIECE_BINARY_PREDICATE(>=, >);
    STRINGPIECE_BINARY_PREDICATE(>, >);
#undef STRINGPIECE_BINARY_PREDICATE

    int compare(const StringPiece &x) const
    {
        int r = memcmp(ptr_, x.ptr_, length_ < x.length_ ? length_ : x.length_);
        if (r == 0)
        {
            if (length_ < x.length_)
                r = -1;
            else if (length_ > x.length_)
                r = +1;
        }
        return r;
    }

    std::string as_string() const
    {
        return std::string(data(), size());
    }

    void CopyToString(std::string *target) const
    {
        target->assign(ptr_, length_);
    }

    // Does "this" start with "x"
    bool starts_with(const StringPiece &x) const
    {
        return ((length_ >= x.length_) && (memcmp(ptr_, x.ptr_, x.length_) == 0));
    }
};

} // namespace muduo

// ------------------------------------------------------------------
// Functions used to create STL containers that use StringPiece
//  Remember that a StringPiece's lifetime had better be less than
//  that of the underlying string or char*.  If it is not, then you
//  cannot safely store a StringPiece into an STL container
// ------------------------------------------------------------------

#ifdef HAVE_TYPE_TRAITS
// This makes vector<StringPiece> really fast for some STL implementations
//在stl中为了提供通用的操作而又不失操作效率，我们使用了一种特殊的技巧，叫做traits的编程技巧
//具体来说，traits就是通过定义一些结构体或类，并利用模板持久化和偏持化的能力，给类赋予一些特性
//这些特性根据类型的一些特性，引发c++的函数重载机制，实现一种该操作因类型不同而异的效果
//
template <>
struct __type_traits<sserver::StringPiece> //类型特性，给这个类了这些特征，这个类放到vector后，可能他的性能会更高
{
    typedef __true_type has_trivial_default_constructor; //具有普通的平凡的构造函数
    typedef __true_type has_trivial_copy_constructor;
    typedef __true_type has_trivial_assignment_operator;
    typedef __true_type has_trivial_destructor;
    typedef __true_type is_POD_type; //这些数据都满足，可以把他看成基本c的数据类型
};
/*
template <typename T>
{
    //对所有类型的操作都是相同的，这就会导致一些类型在这里会出现性能的缺失，挥着某些类型需要特例实现
    //所以就可以给类型加上特征，带吗就可以根据类型的特征，提供特殊的实现,使得通用的代码就不会产生性能损失
    //这里面含有一些多态来实现，
}
*/

#endif

// allow StringPiece to be logged
std::ostream &operator<<(std::ostream &o, const sserver::StringPiece &piece);
#endif // MUDUO_BASE_STRINGPIECE_H
