# 原定计划 #

进一步完善runtime lib，使得更复杂的go可以运行于ucore上。完善ucore的线程机制，使之与go的线程调度匹配。（第9～13周）


# 项目进展 #

  * 新添加了部分重编译的包：runtime、reflect、strings、math、strconv、unicode、utf8、sync/atomic、sync、syscall、os、io、bytes、fmt、path/filepath、io/ioutil、sort、container/heap、time，其中syscall、time、path、os与系统相关，被添加到patch中；
  * runtime实现了内置的函数runtime·write，在syscall和fmt被部分重编译为ucoer target后，实现了fmt的Println，该函数不使用runtime·write，而是调用syscall中的标准接口（见后文测试代码部分）。syscall的进一步完善将包括调用号的对应（zsysnum\_ucore\_386.go）、调用函数的对应（zsyscall\_ucore\_386.go），目前的syscall只实现到putc；
  * 由于要进一步进行代码测试，因此花了些时间看完了《Learning Go》；
  * 进行了channel（sieve1.go）和goroutines（goroutines.go）的测试，并且发现已经可以通过测试。测试代码见下文。但是仅通过这两个测试不能很好的反映goroutines的并发性，因此又自行编写了peter.go，该程序调用了sleep，却因此引发了clone的系统调用。内部机制尚不明确。代码也在下文中。

# 下一步工作 #
  * 实现clone、sleep等线程相关的函数；
  * 进一步挖掘Go on ucore的扩展能力；

# 测试代码 #
  * Another Hello world in package "fmt": hw1.go
```
package main

import "fmt"

func main() {
	fmt.Println("Hello, World!")
}
```
  * sieve1.go
```
// $G $D/$F.go && $L $F.$A && ./$A.out

// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Generate primes up to 100 using channels, checking the results.
// This sieve consists of a linear chain of divisibility filters,
// equivalent to trial-dividing each n by all primes p ≤ n.

package main

// Send the sequence 2, 3, 4, ... to channel 'ch'.
func Generate(ch chan<- int) {
	for i := 2; ; i++ {
		ch <- i // Send 'i' to channel 'ch'.
	}
}

// Copy the values from channel 'in' to channel 'out',
// removing those divisible by 'prime'.
func Filter(in <-chan int, out chan<- int, prime int) {
	for i := range in { // Loop over values received from 'in'.
		if i%prime != 0 {
			out <- i // Send 'i' to channel 'out'.
		}
	}
}

// The prime sieve: Daisy-chain Filter processes together.
func Sieve(primes chan<- int) {
	ch := make(chan int) // Create a new channel.
	go Generate(ch)      // Start Generate() as a subprocess.
	for {
		// Note that ch is different on each iteration.
		prime := <-ch
		primes <- prime
		ch1 := make(chan int)
		go Filter(ch, ch1, prime)
		ch = ch1
	}
}

func main() {
	primes := make(chan int)
	go Sieve(primes)
	a := []int{2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97}
	for i := 0; i < len(a); i++ {
		if x := <-primes; x != a[i] {
			println(x, " != ", a[i])
			panic("fail")
		} 
	}
}
```
  * goroutines.go
```
// $G $D/$F.go && $L $F.$A && ./$A.out

// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// make a lot of goroutines, threaded together.
// tear them down cleanly.

package main

import (
	"os"
	"strconv"
)

func f(left, right chan int) {
	left <- <-right
}

func main() {
	var n = 10000
	if len(os.Args) > 1 {
		var err os.Error
		n, err = strconv.Atoi(os.Args[1])
		if err != nil {
			print("bad arg\n")
			os.Exit(1)
		}
	}
	leftmost := make(chan int)
	right := leftmost
	left := leftmost
	for i := 0; i < n; i++ {
		right = make(chan int)
		go f(left, right)
		left = right
	}
	go func(c chan int) { c <- 1 }(right)
	<-leftmost
}
```
  * peter.go
```
package main

import (
	"fmt"
	"time"
)

var c chan int

func ready(index int) {
	time.Sleep(5e9)
	fmt.Println(index)
	c <- 1
}

func main() {
	c = make(chan int)
	for i:= 1; i < 100; i++ {
		go ready(i)
	}
	for i:= 1; i < 100; i++ {
		<- c
	}
}
```