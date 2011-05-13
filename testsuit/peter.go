package main

import (
	//"fmt"
	"time"
)

var c chan int

func ready(index int) {
	time.Sleep(5e9)
	print(index)
	c <- 1
}

func main() {
	total := 5
	c = make(chan int)
	for i := 0; i < total; i++ {
		go ready(i)
	}
	for i := 0; i < total; i++ {
		<- c
	}
}
