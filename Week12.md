# 项目进展 #

  1. 完善了工具链，现在，使用如下命令可以获得更多的演示方式：`./demo.sh --help`；
  1. 修改了Lock / Unlock机制；
  1. 熟悉了Go内部的调度算法；
  1. 熟悉了Go的用户层锁机制；
  1. 实现了Sleep；
  1. 已经可以正常运行peter.go，但是在使用fmt.Println方式打印的时候会出现部分格式错误，详见下文；

# 探索过程 #
> 本周目前主要解决了上周最后提到的问题。

> 问题回顾：_目前的代码会卡在Go自身的调度算法处，出错信息为找不到下一个等待运行的Goroutine。_

> 因为探索过程比较艰辛，特此记录：
  * 首先怀疑Lock / Unlock机制存在问题，于是读了package runtime中的thread.c，纠正了先前对semaphore的误解，并进一步完善了针对ucore的Lock / Unlock机制，但是问题依然存在；
  * 开始研究调度器错误产生的原因，于是开始读Go的调度算法，（一千多行... T T），发现396行将m->nextg设为nil，但是之后407行又以此为throw的依据，中间也没有赋值操作，觉得甚是奇怪；
  * 奇怪之余，开始追踪Linux下同一段代码的执行，发现405行会造成当前线程被切换，于是着手看ucore是否也有一样特性；
  * 添加所有Lock机制的调试信息打印，伪描述如下：
```
pid[4]:sem1 <- sem_init(0)
pid[4]:sem_wait(sem1)
pid[5]:sem_post(sem1)
pid[5]:sem2 <- sem_init(0)
pid[5]:sem_wait(sem2)
```
> 按道理此时应该切换回pid`[4`]，但是实际却停留在pid`[5`]，并最终造成了throw；
  * 开始怀疑ucore中sem的实现是否有缺陷，于是另写了一个小程序用于模拟本情况：
```
#include <stdio.h>
#include <ulib.h>
#include <thread.h>

sem_t sem1;
sem_t sem2;

int myThread(void *arg)
{
	sleep(10);
	sem_post(sem1);
	sem2 = sem_init(0);
	sem_wait(sem2);
	cprintf("Child here!\n");
	return 0;
}

int main()
{
	thread_t tid;
	sem1 = sem_init(0);
	thread(myThread, NULL, &tid);
	sem_wait(sem1);
	cprintf("Father here!\n");
	sem_post(sem2);
	
	int exit_code;
	thread_wait(&tid, &exit_code);
	return 0;
}
```
> > 但是发现运行没有问题... - -
  * 这样，最后锁定问题在于调度时出现问题，而且是上述程序不会遭遇的问题。猛然意识到是和ldt相关。经检查发现，ucore在trap的时候并不保存fs和gs，但是gs恰是Go寻址的依据，由于没有切换pid`[4`]的gs，因此沿用原来的gs值就会依旧停留在pid`[5`]中运行。之所以4到5可以正常切换是因为5实际是被clone出来的，gs在set\_ldt时就被初始化；
  * 增添fs，gs的保存和恢复，问题解决；


# 遇到的问题 #
  1. peter.go目前使用print函数打印，这样的方式不会有问题，但是如果是使用fmt.Println打印就会出现格式错误，具体表现为每个goroutines只打印首字符就被调度，因此5个hi会变成hhhhhiiiii。目前怀疑与package io有关，正在进行进一步研究；
  1. 上周提到的ldt问题目前要摆上台面讨论了；
  1. 有两种形式的exit: exit current os thread & exit thread group，现在只有前一种实现了，因此peter.go还不能正常退出；