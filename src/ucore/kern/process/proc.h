#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <types.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>
#include <unistd.h>
#include <sem.h>
#include <event.h>

enum proc_state {
	PROC_UNINIT = 0,
	PROC_SLEEPING,
	PROC_RUNNABLE,
	PROC_ZOMBIE,
};

struct context {
	uint32_t eip;
	uint32_t esp;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t esi;
	uint32_t edi;
	uint32_t ebp;
};

#define PROC_NAME_LEN               15
#define MAX_PROCESS					4096
#define MAX_PID						(MAX_PROCESS * 2)

extern list_entry_t proc_list;
extern list_entry_t proc_mm_list;

struct inode;
struct fs_struct;

struct proc_struct {
	enum proc_state state;
	int pid;
	int runs;
	uintptr_t kstack;
	volatile bool need_resched;
	struct proc_struct *parent;
	struct mm_struct *mm;
	struct context context;
	struct trapframe *tf;
	uintptr_t cr3;
	uint32_t flags;
	char name[PROC_NAME_LEN + 1];
	list_entry_t list_link;
	list_entry_t hash_link;
	int exit_code;
	uint32_t wait_state;
	struct proc_struct *cptr, *yptr, *optr;
	list_entry_t thread_group;
	struct run_queue *rq;
	list_entry_t run_link;
	int time_slice;
	sem_queue_t *sem_queue;
	event_t event_box;
	struct fs_struct *fs_struct;
};

#define PF_EXITING					0x00000001		// getting shutdown

#define WT_CHILD					(0x00000001 | WT_INTERRUPTED)
#define WT_TIMER					(0x00000002 | WT_INTERRUPTED)
#define WT_KSWAPD					 0x00000003
#define WT_KBD						(0x00000004 | WT_INTERRUPTED)
#define WT_KSEM						 0x00000100
#define WT_USEM						(0x00000101 | WT_INTERRUPTED)
#define WT_EVENT_SEND				(0x00000110 | WT_INTERRUPTED)
#define WT_EVENT_RECV				(0x00000111 | WT_INTERRUPTED)
#define WT_MBOX_SEND				(0x00000120 | WT_INTERRUPTED)
#define WT_MBOX_RECV				(0x00000121 | WT_INTERRUPTED)
#define WT_PIPE						(0x00000200 | WT_INTERRUPTED)
#define WT_INTERRUPTED				 0x80000000

#define le2proc(le, member)			\
	to_struct((le), struct proc_struct, member)

extern struct proc_struct *idleproc, *initproc, *current;
extern struct proc_struct *kswapd;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);
int do_execve(const char *name, int argc, const char **argv);
int do_yield(void);
int do_wait(int pid, int *code_store);
int do_kill(int pid);
int do_brk(uintptr_t *brk_store);
int do_sleep(unsigned int time);
int do_mmap(uintptr_t *addr_store, size_t len, uint32_t mmap_flags);
int do_munmap(uintptr_t addr, size_t len);
int do_shmem(uintptr_t *addr_store, size_t len, uint32_t mmap_flags);
int do_modify_ldt(int func, void* ptr, uint32_t bytecount);

#endif /* !__KERN_PROCESS_PROC_H__ */

