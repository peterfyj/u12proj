# 原定计划 #

  * (Week4-Week5) 了解实验环境，明了现有环境距离将go中的“hello world”搬上ucore所欠缺的部分。

# 项目进展 #

  1. 完成交叉编译环境的搭建，可以生成宿主系统为linux、目标系统为ucore的go语言交叉编译器；
  1. 研究了go程序在qemu中ucore的用户态模式调试方法。能够很方便地定位到相关地方展开调试；
  1. 具体调研了Hello World所需syscall的实现方法。

# 实现分析 #

  1. go的runtime中绕过的libc，直接调用了操作系统相关的syscall。由于linux系统的syscall与ucore有一定的相似度，可以直接使用的一些syscall如下：
    1. 内存管理方面使用mmap和munmap。由于go也没有文件映射的需要，ucore不需要增加这方面支持；
    1. 线程linux使用的是futex。rtems系统的go语言runtime库中使用的是semaphore信号量。由于ucore中也有相关的semaphore信号量，可以进行一定的尝试。
    1. 需要注意到ucore的syscall寄存器顺序与linux有所不同，对于简单syscall也需要进行细微的调整。

# 问题描述 #

  1. ucore的mmap和munmap和linux的参数有所不同，是否行为表现不同也有待进一步考察；同理，semaphore也需要进行更审慎的考虑；
  1. 在go的386体系中用到了ldt表，作为thread local storage的支持。而ucore没有相关的syscall，可能需要额外的支持；

# 下一步工作 #

> 开始进行runtime库的修改，对Hello World在ucore中单步跟踪进行调试，一个函数一个函数进行更改。






