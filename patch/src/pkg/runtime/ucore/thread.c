// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "runtime.h"
#include "defs.h"
#include "os.h"
#include "stack.h"

extern SigTab runtime·sigtab[];

// Thread-safe allocation of a semaphore.
// Psema points at a kernel semaphore key.
// It starts out zero, meaning no semaphore.
// Fill it in, being careful of others calling initsema
// simultaneously.
static void
initsema(uint32 *psema, uint32 value)
{
	uint32 sema;

	if(*psema != 0)	// already have one
		return;

	sema = runtime·sem_init(value);
	
	// [MARK-PETER] runtime.cas:
	// if (*psema == 0) {
	//   *psema = sema;
	//   return true;
	// }
	// else return false;
	if(!runtime·cas(psema, 0, sema)){
		// Someone else filled it in.  Use theirs.
		runtime·sem_free(sema);
		return;
	}
}

// Blocking locks.

// Implement Locks, using semaphores.
// l->key is the number of threads who want the lock.
// In a race, one thread increments l->key from 0 to 1
// and the others increment it from >0 to >1.  The thread
// who does the 0->1 increment gets the lock, and the
// others wait on the semaphore.  When the 0->1 thread
// releases the lock by decrementing l->key, l->key will
// be >0, so it will increment the semaphore to wake up
// one of the others.  This is the same algorithm used
// in Plan 9's user-level locks.


void
runtime·lock(Lock *l)
{
	if(m->locks < 0)
		runtime·throw("lock count");
	m->locks++;
	
	if(runtime·xadd(&l->key, 1) > 1) {	// someone else has it; wait
		// Allocate semaphore if needed.
		if(l->sema == 0)
			initsema(&l->sema, 0);
		runtime·sem_wait(l->sema, 0);
	}
}

void
runtime·unlock(Lock *l)
{
	m->locks--;
	if(m->locks < 0)
		runtime·throw("lock count");
	
	if(runtime·xadd(&l->key, -1) > 0) {	// someone else is waiting
		// Allocate semaphore if needed.
		if(l->sema == 0)
			initsema(&l->sema, 0);
		runtime·sem_post(l->sema);
	}
}

void
runtime·destroylock(Lock *l)
{
	if(l->sema != 0) {
		runtime·sem_free(l->sema);
		l->sema = 0;
	}
}


// Clone, the Linux rfork.
enum
{
	CLONE_VM = 0x100,
	CLONE_FS = 0x200,
	CLONE_FILES = 0x400,
	CLONE_SIGHAND = 0x800,
	CLONE_PTRACE = 0x2000,
	CLONE_VFORK = 0x4000,
	CLONE_PARENT = 0x8000,
	CLONE_THREAD = 0x10000,
	CLONE_NEWNS = 0x20000,
	CLONE_SYSVSEM = 0x40000,
	CLONE_SETTLS = 0x80000,
	CLONE_PARENT_SETTID = 0x100000,
	CLONE_CHILD_CLEARTID = 0x200000,
	CLONE_UNTRACED = 0x800000,
	CLONE_CHILD_SETTID = 0x1000000,
	CLONE_STOPPED = 0x2000000,
	CLONE_NEWUTS = 0x4000000,
	CLONE_NEWIPC = 0x8000000,
};

void
runtime·newosproc(M *m, G *g, void *stk, void (*fn)(void))
{
	int32 ret;
	int32 flags;

	/*
	 * note: strace gets confused if we use CLONE_PTRACE here.
	 */
	flags = CLONE_VM	/* share memory */
		| CLONE_FS	/* share cwd, etc */
		| CLONE_FILES	/* share fd table */
		| CLONE_SIGHAND	/* share sig handler table */
		| CLONE_THREAD	/* revisit - okay for now */
		;

	m->tls[0] = m->id;	// so 386 asm can find it
	if(0){
		runtime·printf("newosproc stk=%p m=%p g=%p fn=%p clone=%p id=%d/%d ostk=%p\n",
			stk, m, g, fn, runtime·clone, m->id, m->tls[0], &m);
	}

	ret = runtime·clone(flags, stk, m, g, fn);

	if(ret < 0)
		*(int32*)123 = 123;
}

void
runtime·osinit(void)
{
}

void
runtime·goenvs(void)
{
	runtime·goenvs_unix();
}

// Called to initialize a new m (including the bootstrap m).
void
runtime·minit(void)
{
	// Initialize signal handling.
	m->gsignal = runtime·malg(32*1024);	// OS X wants >=8K, Linux >=2K
	runtime·signalstack(m->gsignal->stackguard - StackGuard, 32*1024);
}

// User-level semaphore implementation:
// try to do the operations in user space on u,
// but when it's time to block, fall back on the kernel semaphore k.
// This is the same algorithm used in Plan 9.
void
runtime·usemacquire(Usema *s)
{
	if((int32)runtime·xadd(&s->u, -1) < 0) {
		if(s->k == 0)
			initsema(&s->k, 0);
		runtime·sem_wait(s->k, 0);
	}
}

void
runtime·usemrelease(Usema *s)
{
	if((int32)runtime·xadd(&s->u, 1) <= 0) {
		if(s->k == 0)
			initsema(&s->k, 0);
		runtime·sem_post(s->k);
	}
}


// Event notifications.
void
runtime·noteclear(Note *n)
{
	n->wakeup = 0;
}

void
runtime·notesleep(Note *n)
{
	while(!n->wakeup)
		runtime·usemacquire(&n->sema);
}

void
runtime·notewakeup(Note *n)
{
	n->wakeup = 1;
	runtime·usemrelease(&n->sema);
}


void
runtime·sigpanic(void)
{
	switch(g->sig) {
	case SIGBUS:
		if(g->sigcode0 == BUS_ADRERR && g->sigcode1 < 0x1000)
			runtime·panicstring("invalid memory address or nil pointer dereference");
		runtime·printf("unexpected fault address %p\n", g->sigcode1);
		runtime·throw("fault");
	case SIGSEGV:
		if((g->sigcode0 == 0 || g->sigcode0 == SEGV_MAPERR || g->sigcode0 == SEGV_ACCERR) && g->sigcode1 < 0x1000)
			runtime·panicstring("invalid memory address or nil pointer dereference");
		runtime·printf("unexpected fault address %p\n", g->sigcode1);
		runtime·throw("fault");
	case SIGFPE:
		switch(g->sigcode0) {
		case FPE_INTDIV:
			runtime·panicstring("integer divide by zero");
		case FPE_INTOVF:
			runtime·panicstring("integer overflow");
		}
		runtime·panicstring("floating point error");
	}
	runtime·panicstring(runtime·sigtab[g->sig].name);
}
