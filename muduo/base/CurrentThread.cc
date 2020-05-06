// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/CurrentThread.h"

#include <cxxabi.h>
#include <execinfo.h>
#include <stdlib.h>

namespace muduo
{
namespace CurrentThread
{
//__thread修饰的变量是线程局部存储的,每个线程都有一份，但是只能修饰基础变量，还只能是编译期常量
__thread int t_cachedTid = 0;       //线程真实的pid(tid)的缓存，提哦啊好获取tid的效率，减少系统的次数
__thread char t_tidString[32];      //tid的字符串表示形式
__thread int t_tidStringLength = 6; //字符串长度是6
__thread const char *t_threadName = "unknown";
const bool sameType = std::is_same<int, pid_t>::value; //C++11中的std::is_same可以判断输入的类型是否是指定的模板类型。
static_assert(sameType, "Thread.cpp");                 //编译时就判断能不能编译

std::string stackTrace(bool demangle)
{
    std::string stack;
    const int max_frames = 200;
    void* frame[max_frames];
    int nptrs = ::backtrace(frame, max_frames);
    char** strings = ::backtrace_symbols(frame, nptrs);
    if (strings)
    {
        size_t len = 256;
        char* demangled = demangle ? static_cast<char*>(::malloc(len)) : nullptr;
        for (int i = 1; i < nptrs; ++i)  // skipping the 0-th, which is this function
        {
            if (demangle)
            {
                // https://panthema.net/2008/0901-stacktrace-demangled/
                // bin/exception_test(_ZN3Bar4testEv+0x79) [0x401909]
                char* left_par = nullptr;
                char* plus = nullptr;
                for (char* p = strings[i]; *p; ++p)
                {
                    if (*p == '(')
                        left_par = p;
                    else if (*p == '+')
                        plus = p;
                }

                if (left_par && plus)
                {
                    *plus = '\0';
                    int status = 0;
                    char* ret = abi::__cxa_demangle(left_par+1, demangled, &len, &status);
                    *plus = '+';
                    if (status == 0)
                    {
                        demangled = ret;  // ret could be realloc()
                        stack.append(strings[i], left_par+1);
                        stack.append(demangled);
                        stack.append(plus);
                        stack.push_back('\n');
                        continue;
                    }
                }
            }
            // Fallback to mangled names
            stack.append(strings[i]);
            stack.push_back('\n');
        }
        free(demangled);
        free(strings);
    }
    return stack;
}

}  // namespace CurrentThread
}  // namespace muduo
