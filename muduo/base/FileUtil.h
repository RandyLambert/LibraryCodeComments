// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_BASE_FILEUTIL_H
#define MUDUO_BASE_FILEUTIL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/StringPiece.h"
#include <sys/types.h>  // for off_t

namespace muduo
{
namespace FileUtil
{

//FileUtil是最底层的文件类，封装了Log文件的打开、写入并在类析构的时候关闭文件，底层使用了标准IO，该append函数直接向文件写。
// read small file < 64KB
class ReadSmallFile
{
public:
  ReadSmallFile(StringArg filename);
  ~ReadSmallFile();

  // return errno
  template <typename String>
  int readToString(int maxSize,
                   String *content,
                   int64_t *fileSize,
                   int64_t *modifyTime,
                   int64_t *createTime);

  /// Read at maxium kBufferSize into buf_
  // return errno
  int readToBuffer(int *size);

  const char *buffer() const { return buf_; }

  static const int kBufferSize = 64 * 1024;

private:
  int fd_;
  int err_;
  char buf_[kBufferSize];
};

// read the file content, returns errno if error happens.
template <typename String>
int readFile(StringArg filename, //从这个文件内读取
             int maxSize,        //读文件类，出错返回errno
             String *content,    //从文件中读取，保存到这个字符串中
             int64_t *fileSize = NULL,
             int64_t *modifyTime = NULL,
             int64_t *createTime = NULL)
{
  ReadSmallFile file(filename);
  return file.readToString(maxSize, content, fileSize, modifyTime, createTime); //调用读文件函数
}

// not thread safe
class AppendFile
{
public:
  explicit AppendFile(StringArg filename);
  AppendFile &operator=(AppendFile &) = delete;
  AppendFile(const AppendFile &) = delete;

  ~AppendFile();

  void append(const char *logline, const size_t len);

  void flush();

  size_t writtenBytes() const { return writtenBytes_; } //已经写入文件的大小

private:
  size_t write(const char *logline, size_t len);

  FILE *fp_;               //文件指针的缓冲区指针
  char buffer_[64 * 1024]; //默认是一个这么大，fflush有两种前提，一个是缓冲区满，一个是直接调用fflush
  size_t writtenBytes_;
};
} // namespace FileUtil

}  // namespace muduo

#endif  // MUDUO_BASE_FILEUTIL_H

