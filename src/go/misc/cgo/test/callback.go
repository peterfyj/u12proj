package cgotest

/*
void callback(void *f);
void callGoFoo(void) {
	extern void goFoo(void);
	goFoo();
}
*/
import "C"

import (
	"runtime"
	"testing"
	"unsafe"
)

// nestedCall calls into C, back into Go, and finally to f.
func nestedCall(f func()) {
	// NOTE: Depends on representation of f.
	// callback(x) calls goCallback(x)
	C.callback(*(*unsafe.Pointer)(unsafe.Pointer(&f)))
}

//export goCallback
func goCallback(p unsafe.Pointer) {
	(*(*func())(unsafe.Pointer(&p)))()
}

func TestCallback(t *testing.T) {
	var x = false
	nestedCall(func(){x = true})
	if !x {
		t.Fatal("nestedCall did not call func")
	}
}

func TestCallbackGC(t *testing.T) {
	nestedCall(runtime.GC)
}

func lockedOSThread() bool  // in runtime.c

func TestCallbackPanic(t *testing.T) {
	// Make sure panic during callback unwinds properly.
	if lockedOSThread() {
		t.Fatal("locked OS thread on entry to TestCallbackPanic")
	}
	defer func() {
		s := recover()
		if s == nil {
			t.Fatal("did not panic")
		}
		if s.(string) != "callback panic" {
			t.Fatal("wrong panic:", s)
		}
		if lockedOSThread() {
			t.Fatal("locked OS thread on exit from TestCallbackPanic")
		}
	}()
	nestedCall(func(){panic("callback panic")})
	panic("nestedCall returned")
}

func TestCallbackPanicLoop(t *testing.T) {
	// Make sure we don't blow out m->g0 stack.
	for i := 0; i < 100000; i++ {
		TestCallbackPanic(t)
	}
}

func TestCallbackPanicLocked(t *testing.T) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	if !lockedOSThread() {
		t.Fatal("runtime.LockOSThread didn't")
	}
	defer func() {
		s := recover()
		if s == nil {
			t.Fatal("did not panic")
		}
		if s.(string) != "callback panic" {
			t.Fatal("wrong panic:", s)
		}
		if !lockedOSThread() {
			t.Fatal("lost lock on OS thread after panic")
		}
	}()
	nestedCall(func(){panic("callback panic")})
	panic("nestedCall returned")
}

// Callback with zero arguments used to make the stack misaligned,
// which broke the garbage collector and other things.
func TestZeroArgCallback(t *testing.T) {
	defer func() {
		s := recover()
		if s != nil {
			t.Fatal("panic during callback:", s)
		}
	}()
	C.callGoFoo()
}

//export goFoo
func goFoo() {
	x := 1
	for i := 0; i < 10000; i++ {
		// variadic call mallocs + writes to 
		variadic(x, x, x)
		if x != 1 {
			panic("bad x")
		}
	}
}

func variadic(x ...interface{}) {}

func TestBlocking(t *testing.T) {
	c := make(chan int)
	go func() {
		for i := 0; i < 10; i++ {
			c <- <-c
		}
	}()
	nestedCall(func(){
		for i := 0; i < 10; i++ {
			c <- i
			if j := <-c; j != i {
				t.Errorf("out of sync %d != %d", j, i)
			}
		}
	})
}
