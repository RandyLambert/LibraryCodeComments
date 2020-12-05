### muduo学习
---
#### 前言：
寒假在学习muduo的时候，将muduo中net库和base库源码进行了注释，基本所有的重要代码都进行了详细注释，比较重要的类比如Eventloop类，Channel类部分函数每行都进行了注释，还改进了几个小功能，比如Exception类增加了处理函数名的功能，在使用时看起来更加方便。

#### muduo 各个类的功能：

##### base 库：

![base](https://img-blog.csdnimg.cn/20200314174126157.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3dlaXhpbl80MzU3NDk2Mg==,size_16,color_FFFFFF,t_70 "base 库")

##### net 库：

![net](https://img-blog.csdnimg.cn/20200314174205647.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3dlaXhpbl80MzU3NDk2Mg==,size_16,color_FFFFFF,t_70 "net 库")

#### 对于muduo的看法：

- 很佩服陈硕神犇的设计功力，太强了，在很多地方的处理兼顾高效和优雅，非常值得学习，在之后我肯定还要把《Linux多线程服务端编程》这本书再看好几遍。
- muduo整体代码写于10,11年，当时使用的很多boost库的地方现在在标准库已经实现，个人建议能不用boost库就不用boost库，倒不是说不好用，甚至在有些时候boost库的实现比标准库的实现效率更高，只是如果为了一两处的性能优势引入了boost库，如果在运行的环境里没有装boost库就麻烦了，这一点在最新版的muduo中也有所体现。
- muduo中有的地方代码，现在看上去就有点多余了，例如Atomic类的封装，在c++11的标准库已经实现了原子操作类，所以如果是在自己写的项目里，现在就可以不用自己造轮子了。
- muduo里面也有我个人感觉设计不合理的地方，比如定时器的实现，使用了set，个人感觉使用优先队列更加方便，都是红黑树，理论上两者效率差距不大，使用优先队列更加简便。

---
#### 备注
由于开始学的时候是在网上下载的muduo源码，版本相对较老，所以当时就自作主张的将里面使用boost库的地方尽量改用了STL库，后来看在github上看到最新的muduo已经将使用boost库的地方改为了使用STL库，还支持了ipv6，修改了一些bug，我就将寒假我加过注释的muduo和最新版的muduo进行了整合，可能里面有细节会出现出入，所以不建议直接用此代码进行运行，仅做学习使用，熟悉框架。
