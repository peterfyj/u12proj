// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "runtime.h"
#include "defs.h"
#include "signals.h"
#include "os.h"

void
runtime·dumpregs(Sigcontext *r)
{
	runtime·printf("trap    %x\n", r->trap_no);
	runtime·printf("error   %x\n", r->error_code);
	runtime·printf("oldmask %x\n", r->oldmask);
	runtime·printf("r0      %x\n", r->arm_r0);
	runtime·printf("r1      %x\n", r->arm_r1);
	runtime·printf("r2      %x\n", r->arm_r2);
	runtime·printf("r3      %x\n", r->arm_r3);
	runtime·printf("r4      %x\n", r->arm_r4);
	runtime·printf("r5      %x\n", r->arm_r5);
	runtime·printf("r6      %x\n", r->arm_r6);
	runtime·printf("r7      %x\n", r->arm_r7);
	runtime·printf("r8      %x\n", r->arm_r8);
	runtime·printf("r9      %x\n", r->arm_r9);
	runtime·printf("r10     %x\n", r->arm_r10);
	runtime·printf("fp      %x\n", r->arm_fp);
	runtime·printf("ip      %x\n", r->arm_ip);
	runtime·printf("sp      %x\n", r->arm_sp);
	runtime·printf("lr      %x\n", r->arm_lr);
	runtime·printf("pc      %x\n", r->arm_pc);
	runtime·printf("cpsr    %x\n", r->arm_cpsr);
	runtime·printf("fault   %x\n", r->fault_address);
}

/*
 * This assembler routine takes the args from registers, puts them on the stack,
 * and calls sighandler().
 */
extern void runtime·sigtramp(void);
extern void runtime·sigignore(void);	// just returns
extern void runtime·sigreturn(void);	// calls runtime·sigreturn

String
runtime·signame(int32 sig)
{
	if(sig < 0 || sig >= NSIG)
		return runtime·emptystring;
	return runtime·gostringnocopy((byte*)runtime·sigtab[sig].name);
}

void
runtime·sighandler(int32 sig, Siginfo *info, void *context, G *gp)
{
	Ucontext *uc;
	Sigcontext *r;

	uc = context;
	r = &uc->uc_mcontext;

	if(gp != nil && (runtime·sigtab[sig].flags & SigPanic)) {
		// Make it look like a call to the signal func.
		// Have to pass arguments out of band since
		// augmenting the stack frame would break
		// the unwinding code.
		gp->sig = sig;
		gp->sigcode0 = info->si_code;
		gp->sigcode1 = r->fault_address;
		gp->sigpc = r->arm_pc;

		// If this is a leaf function, we do smash LR,
		// but we're not going back there anyway.
		// Don't bother smashing if r->arm_pc is 0,
		// which is probably a call to a nil func: the
		// old link register is more useful in the stack trace.
		if(r->arm_pc != 0)
			r->arm_lr = r->arm_pc;
		r->arm_pc = (uintptr)runtime·sigpanic;
		return;
	}

	if(runtime·sigtab[sig].flags & SigQueue) {
		if(runtime·sigsend(sig) || (runtime·sigtab[sig].flags & SigIgnore))
			return;
		runtime·exit(2);	// SIGINT, SIGTERM, etc
	}

	if(runtime·panicking)	// traceback already printed
		runtime·exit(2);
	runtime·panicking = 1;

	if(sig < 0 || sig >= NSIG)
		runtime·printf("Signal %d\n", sig);
	else
		runtime·printf("%s\n", runtime·sigtab[sig].name);

	runtime·printf("PC=%x\n", r->arm_pc);
	runtime·printf("\n");

	if(runtime·gotraceback()){
		runtime·traceback((void*)r->arm_pc, (void*)r->arm_sp, (void*)r->arm_lr, gp);
		runtime·tracebackothers(gp);
		runtime·printf("\n");
		runtime·dumpregs(r);
	}

//	breakpoint();
	runtime·exit(2);
}

void
runtime·signalstack(byte *p, int32 n)
{
	Sigaltstack st;

	st.ss_sp = p;
	st.ss_size = n;
	st.ss_flags = 0;
	runtime·sigaltstack(&st, nil);
}

void
runtime·initsig(int32 queue)
{
	static Sigaction sa;

	runtime·siginit();

	int32 i;
	sa.sa_flags = SA_ONSTACK | SA_SIGINFO | SA_RESTORER;
	sa.sa_mask.sig[0] = 0xFFFFFFFF;
	sa.sa_mask.sig[1] = 0xFFFFFFFF;
	sa.sa_restorer = (void*)runtime·sigreturn;
	for(i = 0; i<NSIG; i++) {
		if(runtime·sigtab[i].flags) {
			if((runtime·sigtab[i].flags & SigQueue) != queue)
				continue;
			if(runtime·sigtab[i].flags & (SigCatch | SigQueue))
				sa.sa_handler = (void*)runtime·sigtramp;
			else
				sa.sa_handler = (void*)runtime·sigignore;
			if(runtime·sigtab[i].flags & SigRestart)
				sa.sa_flags |= SA_RESTART;
			else
				sa.sa_flags &= ~SA_RESTART;
			runtime·rt_sigaction(i, &sa, nil, 8);
		}
	}
}
