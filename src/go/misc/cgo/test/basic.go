// Copyright 2010 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Basic test cases for cgo.

package cgotest

/*
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define SHIFT(x, y)  ((x)<<(y))
#define KILO SHIFT(1, 10)

enum E {
	Enum1 = 1,
	Enum2 = 2,
};

typedef unsigned char uuid_t[20];

void uuid_generate(uuid_t x) {
	x[0] = 0;
}

struct S {
	int x;
};

extern enum E myConstFunc(struct S* const ctx, int const id, struct S **const filter);

enum E myConstFunc(struct S *const ctx, int const id, struct S **const filter) { return 0; }

// issue 1222
typedef union {
	long align;
} xxpthread_mutex_t;

struct ibv_async_event {
	union {
		int x;
	} element;
};

struct ibv_context {
	xxpthread_mutex_t mutex;
};
*/
import "C"
import (
	"os"
	"testing"
	"unsafe"
)

const EINVAL = C.EINVAL /* test #define */

var KILO = C.KILO

func uuidgen() {
	var uuid C.uuid_t
	C.uuid_generate(&uuid[0])
}

func Size(name string) (int64, os.Error) {
	var st C.struct_stat
	p := C.CString(name)
	_, err := C.stat(p, &st)
	C.free(unsafe.Pointer(p))
	if err != nil {
		return 0, err
	}
	return int64(C.ulong(st.st_size)), nil
}

func Strtol(s string, base int) (int, os.Error) {
	p := C.CString(s)
	n, err := C.strtol(p, nil, C.int(base))
	C.free(unsafe.Pointer(p))
	return int(n), err
}

func Atol(s string) int {
	p := C.CString(s)
	n := C.atol(p)
	C.free(unsafe.Pointer(p))
	return int(n)
}

func TestConst(t *testing.T) {
	C.myConstFunc(nil, 0, nil)
}

func TestEnum(t *testing.T) {
	if C.Enum1 != 1 || C.Enum2 != 2 {
		t.Error("bad enum", C.Enum1, C.Enum2)
	}
}

func TestAtol(t *testing.T) {
	l := Atol("123")
	if l != 123 {
		t.Error("Atol 123: ", l)
	}
}

func TestErrno(t *testing.T) {
	n, err := Strtol("asdf", 123)
	if n != 0 || err != os.EINVAL {
		t.Error("Strtol: ", n, err)
	}
}

func TestMultipleAssign(t *testing.T) {
	p := C.CString("234")
	n, m := C.strtol(p, nil, 345), C.strtol(p, nil, 10)
	if n != 0 || m != 234 {
		t.Fatal("Strtol x2: ", n, m)
	}
	C.free(unsafe.Pointer(p))
}

var (
	uint  = (C.uint)(0)
	ulong C.ulong
	char  C.char
)

type Context struct {
	ctx *C.struct_ibv_context
}
