# 原定计划 #

  * (Week4-Week5) 了解实验环境，明了现有环境距离将go中的“hello world”搬上ucore所欠缺的部分。

# 项目进展 #

  1. 通过联系Go开发人员，确定了完成项目的总体需求（参见[Requirement](Requirement.md)）
  1. 通过对“Hello World”汇编代码的分析，确定了其需要实现的runtime函数。预计在完成这些函数的ucore实现后，“Hello World”程序的go版本就能在ucore上正常运行。具体函数如下：

|	-	|	morestack	|
|:--|:----------|
|	`*`	|	lock	     |
|	`*`	|	unlock	   |
|	-	|	malloc	   |
|	-	|	free	     |
|	-	|	printstring	|
|	-	|	throwinit	|
|	-	|	throw	    |
|	-	|	malg	     |
|	-	|	runcgo	   |
|	`*`	|	newosproc	|
|	-	|	startpanic	|
|	-	|	printf	   |
|	-	|	dopanic	  |
|	-	|	stackalloc	|
|	-	|	gosave	   |
|	-	|	memclr	   |
|	`*`	|	exit	     |
|	-	|	xadd	     |
|	`*`	|	write	    |
|	`*`	|	gotraceback	|
|	`*`	|	traceback	|
|	-	|	getcallerpc	|
|	-	|	getcallersp	|
|	`*`	|	tracebackothers	|
|	-	|	FixAlloc`_`Init	|
|	-	|	FixAlloc`_`Alloc	|
|	-	|	mallocgc	 |
|	?	|	cas	      |
|	-	|	gcwaiting	|
|	-	|	gosched	  |
|	-	|	SizeToClass	|
|	-	|	class`_`to`_`size	|
|	-	|	MCache`_`!Alloc	|
|	-	|	MHeap`_`!Alloc	|
|	-	|	markspan	 |
|	-	|	markallocated	|
|	-	|	MemProfileRate	|
|	-	|	setblockspecial	|
|	-	|	MProf`_`!Malloc	|
|	-	|	gc	       |
|	-	|	blockspecial	|
|	-	|	mlookup	  |
|	-	|	markfreed	|
|	-	|	checkfreed	|
|	-	|	MHeap`_`LookupMaybe	|
|	-	|	unmarkspan	|
|	-	|	MHeap`_`Free	|
|	-	|	MCache`_`Free	|
|	-	|	MProf`_`Free	|
|	-	|	gogo	     |
|	-	|	MCentral`_`AllocList	|
|	-	|	MCentral`_`FreeList	|
|	-	|	class`_`to`_`transfercount	|
|	-	|	MCentral`_`Init	|
|	-	|	MSpanList`_`Init	|
|	-	|	MHeap`_`AllocLocked	|
|	-	|	MSpanList`_`IsEmpty	|
|	-	|	MHeap`_`AllocLarge	|
|	-	|	MHeap`_`Grow	|
|	-	|	MSpanList`_`Remove	|
|	-	|	MSpan`_`Init	|
|	-	|	MHeap`_`FreeLocked	|
|	-	|	MHeap`_`SysAlloc	|
|	-	|	FixAlloc`_`Free	|
|	-	|	MSpanList`_`Insert	|
|	?	|	casp	     |
|	`*`	|	SysAlloc	 |
|	-	|	mcmp	     |
|	-	|	rnd	      |
|	-	|	memmove	  |
|	?	|	caller	   |
|	-	|	strcmp	   |
|	-	|	getenv	   |
|	-	|	atoi	     |
|	-	|	semacquire	|
|	-	|	MCentral`_`Grow	|
|	-	|	semrelease	|
|	-	|	nanotime	 |
|	-	|	stoptheworld	|
|	-	|	newproc1	 |
|	-	|	starttheworld	|
|	-	|	MCache`_`ReleaseAll	|
|	-	|	MGetSizeClassInfo	|
|	`*`	|	SysFree	  |
|	-	|	findnull	 |
|	`*`	|	gettime	  |
|	`*`	|	noteclear	|
|	`*`	|	notesleep	|
|	-	|	mcpy	     |

> 其中，-代表与操作系统无关（包含了完整实现以及处理器相关的函数），这些函数理论上不需要我们重复实现。而`*`和?的函数就有赖于操作系统的具体实现，完成这些函数理论上就能使“Hello World”正常运行。

# 实现分析 #

> 上面标注需要实现的runtime函数中，有一些能够通过直接调用ucore已有的syscall来完成。由于ucore的syscall与linux十分类似，这些函数往往能够很快地完成（例如write, gettime, exit, SysAlloc, SysFree)。但是对于另外一些涉及进程、线程调度的函数，不仅需要对ucore的相关代码进行深入理解，更可能需要对ucore的相关模块进行改进。因此，“Hello World”完成后，我们就能够使相当一部分go程序在ucore上正常运行。

> 除此之外，我们需要了解go的编译结构，使得我们能在linux上生成ucore-target的go system。第一步，我们可以直接修改go编译器中linux部分的runtime library，这使得我们不需要考虑更改makefile带来的调试问题，集中处理runtime中的bug；在ucore的runtime相对稳定之后，再进行makefile的更改。

# 问题描述 #

  * ucore没有非常详细的文档，需要对源码进行阅读；
  * linux的syscall没有非常详细的介绍，这就要求我们在必要的时候阅读linux内核的相关代码，以理解go中runtime的用意；
  * lock和unlock的具体实现还需要我们进行深入的了解。linux中采用了futex的机制，windows中采用了WaitingForSingleObject函数，具体实现上，我们可能有必要参考这两个系统的相关实现；
  * 虽然ucore已经有了很全面的功能，但是我们在调试时，除了专注于自身代码的调试，仍然需要考虑ucore出现bug的隐患；
  * gdb怎样跟踪ucore中的用户程序？


