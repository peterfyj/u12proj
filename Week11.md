# 项目进展 #

  * 完成了Lock/unlock机制，使用的是ucore的semaphore；
  * syscall get\_tid直接返回pid；
  * clone出的新线程已经可以运行；

# 近期难题 #

  * 之前的fake ldt不是长久之计，因为新clone的线程会占用ldt的（7+tid）表项，目前只是暂时增加gdte的数目来解燃眉之急，等到这段可以正常运行以后再作系统更改；
  * Thread local storage：目前掌握的信息更趋向于暗示操作系统需要提供某种支持来进行indirection，有待进一步验证；

# 备注 #

  * Linux下实验的说明：
在Linux环境下测试peter.go，实验代码如下：
```
package main

import (
	"fmt"
	//"time"
)

var c chan int

func ready(index int) {
	//time.Sleep(5e9)
	fmt.Println(index)
	c <- 1
}

func main() {
	total := 100 
	c = make(chan int)
	for i := 0; i < total; i++ {
		go ready(i)
	}
	for i := 0; i < total; i++ {
		<- c
	}
}
```
在注释保留，也就是每个goroutine不会Sleep的情况下，实际只调用操作系统层clone三次，输出为顺序。如果去掉注释，也就是每个goroutine先使用Sleep睡5秒再输出的情况下，实际调用操作系统层clone一百次，输出为乱序。可见goroutine的行为和其内部包含的实现有关。这个应当比较容易解释：普通的goroutine可以内部实现，但是如果需要操作系统帮助其sleep的则只能依靠操作系统“线程”这一概念，因为操作系统提供的sleep一定是针对线程的。

  * ucore重启的问题现象

经过单步调试，发现系统是由于调用了exit系统调用而重启的。在进入exit系统调用trap后，在call trap时，pc又跳回到了go的程序中的setldt。具体原因有待进一步考察。此外，在使用traceback时，虽然能够正确输出，但是经过一段时间后pc会产生跳变。此后，pc不再与汇编代码中一致，但是读取的代码竟然仍然是正确的。

  * G and M
M代表Machine，G代表Goroutine，一个M中有多个G，并且在runtime/proc.c中实现Go自身的调度。这部分代码长约1300行，但是需要仔细研读才能明白当前症结。目前遇到的问题（下文）怀疑与tls有关。

# 本周主要障碍 #
目前的代码会卡在Go自身的调度算法处，出错信息为找不到下一个等待运行的Goroutine。初步分析表明它和tls或者semaphore有关。