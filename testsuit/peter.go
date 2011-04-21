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
