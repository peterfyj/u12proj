// Copyright 2010 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "runtime.h"
#include "malloc.h"

extern byte end[];
static byte *bloc = { end };

enum
{
	Round = 4095
};

void*
runtime·SysAlloc(uintptr nbytes)
{
	uintptr bl;
	
	// Plan 9 sbrk from /sys/src/libc/9sys/sbrk.c
	bl = ((uintptr)bloc + Round) & ~Round;
	if(runtime·brk_((void*)(bl + nbytes)) < 0)
		return (void*)-1;
	bloc = (byte*)bl + nbytes;
	return (void*)bl;
}

void
runtime·SysFree(void *v, uintptr nbytes)
{
	// from tiny/mem.c
	// Push pointer back if this is a free
	// of the most recent SysAlloc.
	nbytes += (nbytes + Round) & ~Round;
	if(bloc == (byte*)v+nbytes)
		bloc -= nbytes;	
}

void
runtime·SysUnused(void *v, uintptr nbytes)
{
	USED(v, nbytes);
}

void
runtime·SysMap(void *v, uintptr nbytes)
{
	USED(v, nbytes);
}

void*
runtime·SysReserve(void *v, uintptr nbytes)
{
	return runtime·SysAlloc(nbytes);
}
