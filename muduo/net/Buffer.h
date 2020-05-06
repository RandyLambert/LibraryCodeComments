// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_BUFFER_H
#define MUDUO_NET_BUFFER_H

#include "muduo/base/copyable.h"
#include "muduo/base/StringPiece.h"
#include "muduo/base/Types.h"

#include "muduo/net/Endian.h"

#include <algorithm>
#include <vector>

#include <assert.h>
#include <string.h>
//#include <unistd.h>  // ssize_t

namespace muduo
{
namespace net
{

/// A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
///
/// @code
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
/// @endcode
class Buffer : public muduo::copyable
{
 public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
          readerIndex_(kCheapPrepend),
          writerIndex_(kCheapPrepend)
    {
        assert(readableBytes() == 0);
        assert(writableBytes() == initialSize);
        assert(prependableBytes() == kCheapPrepend);
    }

    // implicit copy-ctor, move-ctor, dtor and assignment are fine
    // NOTE: implicit move-ctor is added in g++ 4.6

    void swap(Buffer &rhs) //两个缓冲区交换，因为vector会扩容，导致指针无效，所以size_t来表示位置
    {
        buffer_.swap(rhs.buffer_); //所以交换只需要包位置下标换一下就行
        std::swap(readerIndex_, rhs.readerIndex_);
        std::swap(writerIndex_, rhs.writerIndex_);
    }

    size_t readableBytes() const //返回可读大小
    {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes() const //返回可写大小
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes() const //返回预先空余大小
    {
        return readerIndex_;
    }

    const char *peek() const //返回读的指针
    {
        return begin() + readerIndex_;
    }

    const char *findCRLF() const //寻找结束符
    {
        // FIXME: replace with memmem()?
        const char *crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF + 2);
        return crlf == beginWrite() ? NULL : crlf;
    }

    const char *findCRLF(const char *start) const //从start处往后寻找结束符
    {
        assert(peek() <= start);
        assert(start <= beginWrite());
        // FIXME: replace with memmem()?
        const char *crlf = std::search(start, beginWrite(), kCRLF, kCRLF + 2);
        return crlf == beginWrite() ? NULL : crlf;
    }

    const char *findEOL() const //返回结束处
    {
        const void *eol = memchr(peek(), '\n', readableBytes());
        return static_cast<const char *>(eol);
    }

    const char *findEOL(const char *start) const //从start处返回结束处
    {
        assert(peek() <= start);
        assert(start <= beginWrite());
        const void *eol = memchr(start, '\n', beginWrite() - start);
        return static_cast<const char *>(eol);
    }

    // retrieve returns void, to prevent
    // string str(retrieve(readableBytes()), readableBytes());
    // the evaluation of two functions are unspecified
    void retrieve(size_t len) //取回数据
    {
        assert(len <= readableBytes());
        if (len < readableBytes())
        {
            readerIndex_ += len;
        }
        else
        {
            retrieveAll();
        }
    }

    void retrieveUntil(const char *end) //取回直到某一个位置
    {
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(end - peek());
    }

    void retrieveInt64() //取回八个字节
    {
        retrieve(sizeof(int64_t));
    }

    void retrieveInt32() //4
    {
        retrieve(sizeof(int32_t));
    }

    void retrieveInt16() //2
    {
        retrieve(sizeof(int16_t));
    }

    void retrieveInt8() //1
    {
        retrieve(sizeof(int8_t));
    }

    void retrieveAll() //返回初始位置
    {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    string retrieveAllAsString() //把所有数据取回到一个字符串
    {
        return retrieveAsString(readableBytes());
    }

    string retrieveAsString(size_t len) //从可读处取len长度的字符串
    {
        assert(len <= readableBytes());
        string result(peek(), len); //peek返回的是可读端首
        retrieve(len);              //偏移到len处
        return result;
    }

    StringPiece toStringPiece() const //转换为stringpiece
    {
        return StringPiece(peek(), static_cast<int>(readableBytes()));
    }

    void append(const StringPiece &str) //添加数据
    {
        append(str.data(), str.size());
    }

    void append(const char * /*restrict*/ data, size_t len) //添加数据
    {
        ensureWritableBytes(len); //如果可用空间不足len，则扩充缓冲区位置到len以便可以完全放下数据
        std::copy(data, data + len, beginWrite());
        hasWritten(len);
    }

    void append(const void * /*restrict*/ data, size_t len) //添加数据
    {
        append(static_cast<const char *>(data), len);
    }
    //如果可用空间不足len，则扩充缓冲区位置到len以便可以完全放下数据
    void ensureWritableBytes(size_t len)
    {
        if (writableBytes() < len) //入过可用空间小于len
        {
            makeSpace(len); //扩充空间操作
        }
        assert(writableBytes() >= len); //断言数据可放的下
    }

    char *beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char *beginWrite() const
    {
        return begin() + writerIndex_;
    }

    void hasWritten(size_t len)
    {
        assert(len <= writableBytes());
        writerIndex_ += len;
    }

    void unwrite(size_t len)
    {
        assert(len <= readableBytes());
        writerIndex_ -= len;
    }

    ///
    /// Append int64_t using network endian
    ///
    void appendInt64(int64_t x) //填充64位整数
    {
        int64_t be64 = sockets::hostToNetwork64(x); //转网络字节序
        append(&be64, sizeof be64);
    }

    ///
    /// Append int32_t using network endian
    ///
    void appendInt32(int32_t x) //填充32位整数
    {
        int32_t be32 = sockets::hostToNetwork32(x);
        append(&be32, sizeof be32);
    }

    void appendInt16(int16_t x) //16
    {
        int16_t be16 = sockets::hostToNetwork16(x);
        append(&be16, sizeof be16);
    }

    void appendInt8(int8_t x) //8
    {
        append(&x, sizeof x);
    }

    ///
    /// Read int64_t from network endian
    ///
    /// Require: buf->readableBytes() >= sizeof(int32_t)
    int64_t readInt64() //读取64位
    {
        int64_t result = peekInt64();
        retrieveInt64(); //读走的位置调整
        return result;
    }

    ///
    /// Read int32_t from network endian
    ///
    /// Require: buf->readableBytes() >= sizeof(int32_t)
    int32_t readInt32()
    {
        int32_t result = peekInt32();
        retrieveInt32();
        return result;
    }

    int16_t readInt16()
    {
        int16_t result = peekInt16();
        retrieveInt16();
        return result;
    }

    int8_t readInt8()
    {
        int8_t result = peekInt8();
        retrieveInt8();
        return result;
    }

    ///
    /// Peek int64_t from network endian
    ///
    /// Require: buf->readableBytes() >= sizeof(int64_t)
    int64_t peekInt64() const
    {
        assert(readableBytes() >= sizeof(int64_t));
        int64_t be64 = 0;
        ::memcpy(&be64, peek(), sizeof be64);  //拷贝大小为64位的的到be64
        return sockets::networkToHost64(be64); //转为主机字节序
    }

    ///
    /// Peek int32_t from network endian
    ///
    /// Require: buf->readableBytes() >= sizeof(int32_t)
    int32_t peekInt32() const
    {
        assert(readableBytes() >= sizeof(int32_t));
        int32_t be32 = 0;
        ::memcpy(&be32, peek(), sizeof be32);
        return sockets::networkToHost32(be32);
    }

    int16_t peekInt16() const
    {
        assert(readableBytes() >= sizeof(int16_t));
        int16_t be16 = 0;
        ::memcpy(&be16, peek(), sizeof be16);
        return sockets::networkToHost16(be16);
    }

    int8_t peekInt8() const
    {
        assert(readableBytes() >= sizeof(int8_t));
        int8_t x = *peek();
        return x;
    }

    ///
    /// Prepend int64_t using network endian
    ///
    void prependInt64(int64_t x) //在前面预留空间添加8个字节
    {
        int64_t be64 = sockets::hostToNetwork64(x); //转网络
        prepend(&be64, sizeof be64);
    }

    ///
    /// Prepend int32_t using network endian
    ///
    void prependInt32(int32_t x)
    {
        int32_t be32 = sockets::hostToNetwork32(x);
        prepend(&be32, sizeof be32);
    }

    void prependInt16(int16_t x)
    {
        int16_t be16 = sockets::hostToNetwork16(x);
        prepend(&be16, sizeof be16);
    }

    void prependInt8(int8_t x)
    {
        prepend(&x, sizeof x);
    }

    void prepend(const void * /*restrict*/ data, size_t len) //通过这个函数来时间想前面预留空间加
    {
        assert(len <= prependableBytes());
        readerIndex_ -= len;
        const char *d = static_cast<const char *>(data);
        std::copy(d, d + len, begin() + readerIndex_);
    }

    void shrink(size_t reserve) //伸缩空间，保留reserve个字节
    {
        ensureWritableBytes(readableBytes() + reserve);
        buffer_.resize(readableBytes() + reserve + kInitialSize);
        buffer_.shrink_to_fit();
    }

    size_t internalCapacity() const
    {
        return buffer_.capacity();
    }

    /// Read data directly into buffer.
    ///
    /// It may implement with readv(2)
    /// @return result of read(2), @c errno is saved
    ssize_t readFd(int fd, int *savedErrno);

private:
    char *begin()
    {
        return &*buffer_.begin();
    }

    const char *begin() const
    {
        return &*buffer_.begin();
    }

    void makeSpace(size_t len) //扩充空间
    {
        if (writableBytes() + prependableBytes() < len + kCheapPrepend) //判断扩充空间
        {
            // FIXME: move readable data
            buffer_.resize(writerIndex_ + len); //如果现存缓冲区不够用，进行resize扩容
        }
        else
        {
            // move readable data to the front, make space inside buffer
            assert(kCheapPrepend < readerIndex_); //够用，进行移位存储下来
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_,
                      begin() + writerIndex_,
                      begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
            assert(readable == readableBytes());
        }
    }

private:
    std::vector<char> buffer_; //vector用于代替固定大小数组
    size_t readerIndex_;       //读位置
    size_t writerIndex_;       //写位置

    static const char kCRLF[]; //"\r\n"，http协议
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_BUFFER_H
