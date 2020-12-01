/*
 * @Date: 2020-11-30 21:54:37
 * @LastEditors: OBKoro1
 * @LastEditTime: 2020-12-01 18:21:41
 * @FilePath: /LibraryCodeComments/coroutine/coroutine.c
 * @Auther: SShouxun
 * @GitHub: https://github.com/RandyLambert
 */
#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#if __APPLE__ && __MACH__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

#define STACK_SIZE (1024 * 1024)
#define DEFAULT_COROUTINE 16

struct coroutine;

/**
* 协程调度器的结构体
*/
struct schedule
{
	char stack[STACK_SIZE]; // 运行时栈

	ucontext_t main;	   // 主协程的上下文
	int nco;			   // 当前存活的协程个数
	int cap;			   // 协程管理器的当前最大容量，即可以同时支持多少个协程。可以进行扩容
	int running;		   // 正在运行的协程ID
	struct coroutine **co; // 用于存放协程
};

/*
* 协程结构体
*/
struct coroutine
{
	coroutine_func func;  // 协程所用的函数
	void *ud;			  // 协程参数
	ucontext_t ctx;		  // 协程上下文
	struct schedule *sch; // 该协程所属的调度器
	ptrdiff_t cap;		  // 已经分配的内存大小
	ptrdiff_t size;		  // 当前协程运行时栈，保存起来后的大小
	int status;			  // 协程当前的状态
	char *stack;		  // 当前协程的保存起来的运行时栈
};

/**
 * @description: 新建一个协程 
 * @param {S:"协程调度器",func:"回调函数",ud:"协程参数"}
 * @return {*}
 */
struct coroutine *
_co_new(struct schedule *S, coroutine_func func, void *ud)
{
	struct coroutine *co = malloc(sizeof(*co)); //分配协程空间
	co->func = func; //
	co->ud = ud;
	co->sch = S;
	co->cap = 0;
	co->size = 0;
	co->status = COROUTINE_READY; // 默认的最初状态都是COROUTINE_READY
	co->stack = NULL;
	return co;
}

/**
 * @description: 删除一个协程
 * @param {*}
 * @return {*}
 */
void _co_delete(struct coroutine *co)
{
	free(co->stack); //有栈协程释放协程栈
	free(co);		 //释放协程
}

/**
 * @description: 负责创建和初始化一个协程调度器 
 * @param {*}
 * @return {*}
 */
struct schedule *
coroutine_open(void)
{
	// 这里做的主要就是分配内存，同时赋初值
	struct schedule *S = malloc(sizeof(*S));
	S->nco = 0;
	S->cap = DEFAULT_COROUTINE;
	S->running = -1;
	S->co = malloc(sizeof(struct coroutine *) * S->cap);
	memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
	return S;
}

/**
 * @description: 负责关闭销毁传入的协程调度器以及清理其管理的所有协程
 * @param {S:"调度器"}
 * @return {*}
 */
void coroutine_close(struct schedule *S)
{
	int i;
	// 关闭掉每一个协程
	for (i = 0; i < S->cap; i++)
	{
		struct coroutine *co = S->co[i];

		if (co)
		{
			_co_delete(co);
		}
	}

	// 释放掉
	free(S->co);
	S->co = NULL;
	free(S);
}

/**
 * @description: 负责创建和初始化一个新的协程对象,同事将此协程对象放到协程调度器中
 * @param {S:"该协程所属的调度器",func:"该协程的执行函数",ud:"协程的参数"}
 * @return {新建的协程ID}
 */
int coroutine_new(struct schedule *S, coroutine_func func, void *ud)
{
	struct coroutine *co = _co_new(S, func, ud); //内部函数,创建协程并返回
	if (S->nco >= S->cap) //此处涉及到了扩容,当目前尚在存货的线程个数nco已经等于或者大于协程调度器的容量cap来了,这时需要对协程调度器进行扩容,这里直接是2倍扩容
	{
		// 那么进行扩容
		int id = S->cap; // 新的协程的id直接为当前容量的大小
		// 扩容的方式为，扩大为当前容量的2倍
		S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
		// 初始化内存
		memset(S->co + S->cap, 0, sizeof(struct coroutine *) * S->cap);
		S->co[S->cap] = co; // 新协程放到调度器最后大小的一位
		// 将容量扩大为两倍
		S->cap *= 2;
		// 增加尚未结束运行的协程的个数
		++S->nco;
		return id; //返回新的协程id
	} 
	else
	{
		// 如果目前协程的数量小于调度器的容量，则取一个为NULL的位置，放入新的协程
		int i;
		for (i = 0; i < S->cap; i++) //遍历找到协程为空的协程
		{
			
			// 为什么不 i%S->cap,而是要从nco+i开始呢 
			// 算是一种优化策略，因为前nco有很大概率都非NULL的，直接跳过去更好
			int id = (i + S->nco) % S->cap;
			if (S->co[id] == NULL)
			{
				S->co[id] = co;
				++S->nco;
				return id;
			}
		}
	} //就此一个协程对象被创建完成,此时的协程状态为READY,但是没有正式开始执行
	assert(0); //如果不是上面两种情况直接结束进程
	return -1;
}

/*
 * 通过low32和hi32 拼出了struct schedule的指针，这里为什么要用这种方式，而不是直接传struct schedule*呢？
 * 因为makecontext的函数指针的参数是int可变列表，在64位下，一个int没法承载一个指针
*/
/**
 * @description: 
 * @param {*}
 * @return {*}
 */
static void
mainfunc(uint32_t low32, uint32_t hi32)
{
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
	struct schedule *S = (struct schedule *)ptr;

	int id = S->running;
	struct coroutine *C = S->co[id];
	C->func(S, C->ud); // 中间有可能会有不断的yield
	_co_delete(C);
	S->co[id] = NULL;
	--S->nco;
	S->running = -1;
}

/**
 * @description: 此函数会切入指定的协程中执行,当前正在执行的协程的上下文会被保存起来,同时上下文可以替换为新的协程,该协程的状态被置为RUNNING
 * @param {S 协程调度器,id 协程ID}
 * @return {*}
 */
void coroutine_resume(struct schedule *S, int id) 
{
	assert(S->running == -1); //断言该协程为处于运行状态,避免递归调用协程
	assert(id >= 0 && id < S->cap); //判断该协程调用正确

	// 取出对应id的协程
	struct coroutine *C = S->co[id];
	if (C == NULL)
		return;

	int status = C->status; // 进入coroutine_resume函数的前置状态有两个READY和SUSPEND
	switch (status)
	{
	case COROUTINE_READY:
		// 初始化ucontext_t结构体,将当前的上下文放到C->ctx里面
		getcontext(&C->ctx);
		// 将当前协程的运行时栈的栈顶设置为S->stack
		// 设置当前协程运行时的栈,也就是设置共享栈。（注意，这里是栈顶）
		C->ctx.uc_stack.ss_sp = S->stack;
		C->ctx.uc_stack.ss_size = STACK_SIZE; // 设置栈大小
		C->ctx.uc_link = &S->main; // 如果协程执行完，将切换到主协程(S->main)中执行,如果不设置,则默认是NULL,那么协程执行晚,整个程序就结束了
		S->running = id;           // 更改调度器正在运行的协程的id
		C->status = COROUTINE_RUNNING; // 此协程切换为运行状态

		// 设置执行C->ctx函数, 并将S作为参数传进去
		uintptr_t ptr = (uintptr_t)S;
		// makecontext后面的两个参数是把一个指针掰成两个int作为参数传给mainfunc,二在mainfunc的实现中可以看出来又会把两个int拼成一个struct schedule*
		// 这样做的原因是makecontext的函数指针参数是uint32_t类型,为了考虑兼容性,想在64为系统下使用因此采用这种做法
		makecontext(&C->ctx, (void (*)(void))mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr >> 32));

		// 将当前的上下文放入S->main中，并将C->ctx的上下文替换到当前上下文
		// 这样的话,将会执行新的上下文对应程序,在coroutine中也就是开始执行mainfunc这个函数( mainfunc 是对用户提供的协程函数进行的封装).
		swapcontext(&S->main, &C->ctx);
		break;
	case COROUTINE_SUSPEND: // 此状态出现的原因是由于coroutine_yield可以使当前正在运行的协程切换到朱携程中运行,切换完之后该协程就会进入suspend状态
		// 将协程所保存的栈的内容，拷贝到当前运行时栈中
		// 其中C->size在yield时有保存
		memcpy(S->stack + STACK_SIZE - C->size, C->stack, C->size);
		S->running = id;
		C->status = COROUTINE_RUNNING;
		swapcontext(&S->main, &C->ctx);
		break;
	default:
		assert(0); //不是上面两种情况直接退出
	}
}

/**
 * @description: 将本协程的栈内容保存起来,找到栈顶的位置 
 * @param {top 栈顶 }
 * @return {*}
 */
static void
_save_stack(struct coroutine *C, char *top)
{
	// 这个dummy很关键，是求取整个栈的关键
	// 这个非常经典，涉及到linux的内存分布，栈是从高地址向低地址扩展，因此
	// S->stack + STACK_SIZE就是运行时栈的栈底
	// dummy，此时在栈中，肯定是位于最底的位置的，即栈顶
	// top - &dummy 即整个栈的容量
	char dummy = 0;
	assert(top - &dummy <= STACK_SIZE);
	if (C->cap < top - &dummy)
	{
		free(C->stack);
		C->cap = top - &dummy;
		C->stack = malloc(C->cap);
	}
	C->size = top - &dummy;
	memcpy(C->stack, &dummy, C->size);
}

/**
 * @description: 将当前正在运行的协程让出，切换到主协程上 
 * @param {S 协程调度器}
 * @return {*}
 */
void coroutine_yield(struct schedule *S)
{
	// 取出当前正在运行的协程
	int id = S->running;
	assert(id >= 0);

	struct coroutine *C = S->co[id];
	assert((char *)&C > S->stack);

	// 将当前运行的协程的栈内容保存起来,因为 coroutine 是基于共享栈的,所以协程的栈内容需要单独保存起来
	_save_stack(C, S->stack + STACK_SIZE);

	// 将当前栈的状态改为 挂起
	C->status = COROUTINE_SUSPEND;
	S->running = -1; // s的运行状态改为没有运行任何协程

	// swapcontext 将当前上下文保存到当前协程的 ucontext中,同事替换当前上下文为主协程的上下文,这样的话.当前协程会被挂起,主协程会被继续执行
	swapcontext(&C->ctx, &S->main); // 这里可以看到，只能从协程切换到主协程中

	// 关键点: 如何保存当前协程的运行时栈,也就是如何获取整个栈的内存空间
	// 这里我们需要了解一下栈内存的布局,就是栈生长方向是从高地址往低地址,
	// 我们只需要找到栈的栈顶和栈底的地址,就可以找到整个栈内存空间
	// 在 coroutine 中,协程的运行时栈的内存空间是自己分配的,在 coroutine_resume 阶段设置的C->ctx.uc_stack.ss_sp = S.S->stack
	// 根据以上理论,栈的生长方向是高地址到低地址,因此栈低就是内存地址最大的位置,即 S->stack + STACK_SIZE 就是栈底位置
}

int coroutine_status(struct schedule *S, int id)
{
	assert(id >= 0 && id < S->cap);
	if (S->co[id] == NULL)
	{
		return COROUTINE_DEAD;
	}
	return S->co[id]->status;
}

/**
 * @description: 获取正在运行的协程的ID 
 * @param {S 协程调度器}
 * @return {协程ID}
 */
int coroutine_running(struct schedule *S)
{
	return S->running; 
}
