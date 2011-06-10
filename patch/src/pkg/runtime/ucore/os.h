// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Ucore-specific system calls
uint32 runtime·sem_init(uint32 value);
uint32 runtime·sem_post(uint32 sema);
uint32 runtime·sem_wait(uint32 sema, uint32 timeout);
uint32 runtime·sem_free(uint32 sema);
int32	runtime·clone(int32, void*, M*, G*, void(*)(void));

struct Sigaction;
void	runtime·rt_sigaction(uintptr, struct Sigaction*, void*, uintptr);

void	runtime·sigaltstack(Sigaltstack*, Sigaltstack*);
void	runtime·sigpanic(void);
