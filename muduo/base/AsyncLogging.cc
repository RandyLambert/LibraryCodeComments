// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/AsyncLogging.h"
#include "muduo/base/LogFile.h"
#include "muduo/base/Timestamp.h"

#include <stdio.h>

using namespace muduo;

//生产者消费者模型
//前端(任务线程) 生产者 多个
//后端(日志线程) 消费者 1个
//
//blockingqueue
//消息队列

/* p(semfull) */
/*     p(mutex) */
/*     向队列中取出所有的日志消息,写入到文件中 */
/*     v(mutex) */
/* v(mutex) */

/* p(semfull) */
/*     p(mutex) */
/*     从对列中取道日志消息,写入文件 */
/*     v(mutex) */
/* v(mutex) */
/* 上述操作不好,因为写文件比较频繁,效率比较低 */
/* muduo使用了多缓冲机制 */

AsyncLogging::AsyncLogging(const string& basename,
                           off_t rollSize,
                           int flushInterval)
  : flushInterval_(flushInterval),
    running_(false),
    basename_(basename),
    rollSize_(rollSize),
    thread_(std::bind(&AsyncLogging::threadFunc, this), "Logging"),
    latch_(1),
    mutex_(),
    cond_(mutex_),
    currentBuffer_(new Buffer), //首先准备了两块缓冲区,当前缓冲区
    nextBuffer_(new Buffer),    //预备缓冲区
    buffers_()
{
  currentBuffer_->bzero(); //将缓冲区清零
  nextBuffer_->bzero();
  buffers_.reserve(16); //缓冲区指针列表预留16个空间
}

void AsyncLogging::append(const char* logline, int len)
{
    //因为有多个线程要调用append,所以用mutex保护
  muduo::MutexLockGuard lock(mutex_);
  if (currentBuffer_->avail() > len)//判断一下当前缓冲区是否满了
  {
      //当缓冲区未满,将数据追加到末尾
    currentBuffer_->append(logline, len);
  }
  else
  {
      //当缓冲区已满,将当前缓冲区添加到待写入文件的以填满的缓冲区
    buffers_.push_back(std::move(currentBuffer_));//移动语义

    //将当前缓冲区设置为预备缓冲区
    if (nextBuffer_)
    {
      currentBuffer_ = std::move(nextBuffer_);
    }
    else//相当于nextbuffer也没有了
    {
        //这种情况极少发生,前端写入速度太快了,一下把凉快缓冲区都写完了
        //那么之后分配一个新的缓冲区
      currentBuffer_.reset(new Buffer); // Rarely happens
    }
    currentBuffer_->append(logline, len);
    cond_.notify();//通知后端开始写入日志
  }
}

void AsyncLogging::threadFunc()
{
  assert(running_ == true);
  latch_.countDown();
  LogFile output(basename_, rollSize_, false);
  BufferPtr newBuffer1(new Buffer);//一开始就准备了两块缓冲区
  BufferPtr newBuffer2(new Buffer);
  newBuffer1->bzero();
  newBuffer2->bzero();
  BufferVector buffersToWrite;
  buffersToWrite.reserve(16);
  while (running_)
  {
    assert(newBuffer1 && newBuffer1->length() == 0);
    assert(newBuffer2 && newBuffer2->length() == 0);
    assert(buffersToWrite.empty());

    {
      muduo::MutexLockGuard lock(mutex_);
      if (buffers_.empty())  // unusual usage!(常规用法是while不是if,这里是一个非常规用法,可能会导致虚假唤醒问题)
      {//即使出现虚假唤醒也没有问题
        cond_.waitForSeconds(flushInterval_);//使用条件变量等待,等待前端写满一个或者多个buffer,或者一个超时时间的到来
      }
      buffers_.push_back(std::move(currentBuffer_));//将当前缓冲区一如buffers_
      currentBuffer_ = std::move(newBuffer1);//将空闲的newbuffer1置为当前缓冲区
      buffersToWrite.swap(buffers_);//buffers与bufferstowrite交换,这样后面的代码可以在临界区之外方位bufferstowrite
      if (!nextBuffer_)
      {
        nextBuffer_ = std::move(newBuffer2);//确保前端始终有一个预留的buffer可供调配
                                            //减少前端临街却分配内存的概率,缩短前端
      }
    }

    assert(!buffersToWrite.empty());

    //消息堆积
    //前端陷入死循环,拼命发送日志消息,超过后端的处理能力,这就是典型的生产速度
    //超过消费速度的问题,会造成数据在内存中堆积,严重时会引发性能问题,(可用内存不足)
    //或程序崩溃(分配内存失败)
    if (buffersToWrite.size() > 25)//需要后端写的日志块超过25个,可能是前端日志出现了死循环
    {
      char buf[256];
      snprintf(buf, sizeof buf, "Dropped log messages at %s, %zd larger buffers\n",
               Timestamp::now().toFormattedString().c_str(),
               buffersToWrite.size()-2);//表明是前端出现了死循环
      fputs(buf, stderr);
      output.append(buf, static_cast<int>(strlen(buf)));
      buffersToWrite.erase(buffersToWrite.begin()+2, buffersToWrite.end()); //丢掉多余日志,以腾出内存,只保留2块
    }

    for (const auto& buffer : buffersToWrite) //如果不是大于25块,就把所有的可写入的buffer写入
    {
      // FIXME: use unbuffered stdio FILE ? or use ::writev ?
      output.append(buffer->data(), buffer->length());
    }

    if (buffersToWrite.size() > 2)
    {
      // drop non-bzero-ed buffers, avoid trashing
      buffersToWrite.resize(2); //只保留两个buffer,用于newbuffer1和newbuffer2
    }

    if (!newBuffer1)//如果newbuffer1为空了
    {
      assert(!buffersToWrite.empty());
      newBuffer1 = std::move(buffersToWrite.back()); //就把newbuffer1指向buffertowrite的一块
      buffersToWrite.pop_back(); //然后弹出这块缓冲区
      newBuffer1->reset(); //reset调用的是将缓冲区当前指向指向首部位置,以便交换过来之后是从首部开始写的,把之前的数据覆盖掉
    }

    if (!newBuffer2)
    {
      assert(!buffersToWrite.empty());
      newBuffer2 = std::move(buffersToWrite.back());//同上
      buffersToWrite.pop_back();
      newBuffer2->reset();
    }

    buffersToWrite.clear();//多余的缓冲区清楚掉
    output.flush();//将缓冲区刷新写入
  }
  output.flush();
}

