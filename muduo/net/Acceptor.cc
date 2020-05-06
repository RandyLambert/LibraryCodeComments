// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/Acceptor.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>
#include <fcntl.h>
//#include <sys/types.h>
//#include <sys/stat.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
	: loop_(loop),
	  acceptSocket_(sockets::createNonblockingOrDie()), //创建了一个套接字，监听套接字
	  acceptChannel_(loop, acceptSocket_.fd()),			//关注这个套接字的事件
	  listenning_(false),
	  idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)) //预先准备一个空闲文件描述符，当文件描述符不够用的时候有用
{														 //构造函数，直接就调用了socket，bind，然后设置用户的回调函数
	assert(idleFd_ >= 0);								 //断言这个文件描述符设定成功
	acceptSocket_.setReuseAddr(true);					 //设置地址重复利用，重启服务器时有用
	acceptSocket_.setReusePort(reuseport);				 //端口复用
	acceptSocket_.bindAddress(listenAddr);				 //绑定地址
	acceptChannel_.setReadCallback(						 //设置读的回调函数
		std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
	acceptChannel_.disableAll(); //把所有事件都disable掉
	acceptChannel_.remove();	 //才remove
	::close(idleFd_);			 //关掉fd
}

void Acceptor::listen() //然后listen
{
	loop_->assertInLoopThread();
	listenning_ = true; //监听状态
	acceptSocket_.listen();
	acceptChannel_.enableReading(); //epoll关注这个套接字的读事件，当有事件发生，回调构造函数中那个回调函数
}

void Acceptor::handleRead() //函数返回产生了一个连接套接字，紧接着就是调用Acceptor中的回调函数newConnectionCallback_
{							//被触发以后调用accept()系统调用来接受一个新的连接,同时调用了TcpServer注册的回调函数newConnection,将TcpConneciotn类拉上了舞台
	loop_->assertInLoopThread();
	InetAddress peerAddr; //准备一个对等方地址
	//FIXME loop until no more
	int connfd = acceptSocket_.accept(&peerAddr);
	if (connfd >= 0) //得到了一个链接
	{
		// string hostport = peerAddr.toIpPort();
		// LOG_TRACE << "Accepts of " << hostport;
		if (newConnectionCallback_) //回调上层的用户函数
		{
			newConnectionCallback_(connfd, peerAddr); //TcpServer初始化时调用Acceptor中的setNewConnectionCallback()
													  //函数将newConnection赋值给newConnectionCallback_。也就是说，在Acceptor中一旦accept()系统调用成功返回就立马调用newConnection函数。
													  // newConnecion虽说属于TcpServer，但是newConnection函数的作用是创建了一个类
		}
		else
		{
			sockets::close(connfd); //如果上层没有设定回调函数，就把这个套接字关闭
		}
	}
	else
	{
		LOG_SYSERR << "in Acceptor::handleRead";
		// Read the section named "The special problem of
		// accept()ing when you can't" in libev's doc.
		// By Marc Lehmann, author of livev.
		if (errno == EMFILE) //文件描述符太多了
		{
			::close(idleFd_);									//关闭空闲的文件描述符
			idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL); //是他接收
			::close(idleFd_);									//接收完之后在把他关闭，因为使用的是LT模式，不这样accept会一直触发
			idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
		}
	}
}
