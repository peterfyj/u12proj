#include <types.h>
#include <mmu.h>
#include <memlayout.h>
#include <clock.h>
#include <trap.h>
#include <x86.h>
#include <stdio.h>
#include <kdebug.h>
#include <assert.h>
#include <sync.h>
#include <monitor.h>
#include <console.h>
#include <vmm.h>
#include <proc.h>
#include <sched.h>
#include <unistd.h>
#include <syscall.h>
#include <error.h>

#define TICK_NUM 30

static struct gatedesc idt[256] = {{0}};

static struct pseudodesc idt_pd = {
    sizeof(idt) - 1, (uintptr_t)idt
};

void
idt_init(void) {
    extern uintptr_t __vectors[];
    int i;
    for (i = 0; i < sizeof(idt) / sizeof(struct gatedesc); i ++) {
        SETGATE(idt[i], 1, GD_KTEXT, __vectors[i], DPL_KERNEL);
    }
    SETGATE(idt[T_SYSCALL], 1, GD_KTEXT, __vectors[T_SYSCALL], DPL_USER);
	SETGATE(idt[T_BRKPT], 0, GD_KTEXT, __vectors[T_BRKPT], DPL_USER);
    lidt(&idt_pd);
}

static const char *
trapname(int trapno) {
    static const char * const excnames[] = {
        "Divide error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "BOUND Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection",
        "Page Fault",
        "(unknown trap)",
        "x87 FPU Floating-Point Error",
        "Alignment Check",
        "Machine-Check",
        "SIMD Floating-Point Exception"
    };

    if (trapno < sizeof(excnames)/sizeof(const char * const)) {
        return excnames[trapno];
    }
    if (trapno == T_SYSCALL) {
        return "System call";
    }
    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16) {
        return "Hardware Interrupt";
    }
    return "(unknown trap)";
}

bool
trap_in_kernel(struct trapframe *tf) {
    return (tf->tf_cs == (uint16_t)KERNEL_CS);
}

static const char *IA32flags[] = {
    "CF", NULL, "PF", NULL, "AF", NULL, "ZF", "SF",
    "TF", "IF", "DF", "OF", NULL, NULL, "NT", NULL,
    "RF", "VM", "AC", "VIF", "VIP", "ID", NULL, NULL,
};

void
print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->tf_regs);
    cprintf("  es   0x----%04x\n", tf->tf_es);
    cprintf("  ds   0x----%04x\n", tf->tf_ds);
    cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
    cprintf("  err  0x%08x\n", tf->tf_err);
    cprintf("  eip  0x%08x\n", tf->tf_eip);
    cprintf("  cs   0x----%04x\n", tf->tf_cs);
    cprintf("  flag 0x%08x ", tf->tf_eflags);

    int i, j;
    for (i = 0, j = 1; i < sizeof(IA32flags) / sizeof(IA32flags[0]); i ++, j <<= 1) {
        if ((tf->tf_eflags & j) && IA32flags[i] != NULL) {
            cprintf("%s,", IA32flags[i]);
        }
    }
    cprintf("IOPL=%d\n", (tf->tf_eflags & FL_IOPL_MASK) >> 12);

    if (!trap_in_kernel(tf)) {
        cprintf("  esp  0x%08x\n", tf->tf_esp);
        cprintf("  ss   0x----%04x\n", tf->tf_ss);
    }
}

void
print_regs(struct pushregs *regs) {
    cprintf("  edi  0x%08x\n", regs->reg_edi);
    cprintf("  esi  0x%08x\n", regs->reg_esi);
    cprintf("  ebp  0x%08x\n", regs->reg_ebp);
    cprintf("  oesp 0x%08x\n", regs->reg_oesp);
    cprintf("  ebx  0x%08x\n", regs->reg_ebx);
    cprintf("  edx  0x%08x\n", regs->reg_edx);
    cprintf("  ecx  0x%08x\n", regs->reg_ecx);
    cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static inline void
print_pgfault(struct trapframe *tf) {
    /* error_code:
     * bit 0 == 0 means no page found, 1 means protection fault
     * bit 1 == 0 means read, 1 means write
     * bit 2 == 0 means kernel, 1 means user
     * */
    cprintf("page fault at 0x%08x: %c/%c [%s].\n", rcr2(),
            (tf->tf_err & 4) ? 'U' : 'K',
            (tf->tf_err & 2) ? 'W' : 'R',
            (tf->tf_err & 1) ? "protection fault" : "no page found");
}

static int
pgfault_handler(struct trapframe *tf) {
    extern struct mm_struct *check_mm_struct;
    struct mm_struct *mm;
    if (check_mm_struct != NULL) {
        assert(current == idleproc);
        mm = check_mm_struct;
    }
    else {
        if (current == NULL) {
            print_trapframe(tf);
            print_pgfault(tf);
            panic("unhandled page fault.\n");
        }
        mm = current->mm;
    }
    return do_pgfault(mm, tf->tf_err, rcr2());
}

static void
trap_dispatch(struct trapframe *tf) {
#ifdef DEBUG_PRINT_SYSCALL_NUM
	static const char * const syscallnames[] = {
		[SYS_exit]              "sys_exit",
		[SYS_fork]              "sys_fork",
		[SYS_wait]              "sys_wait",
		[SYS_exec]              "sys_exec",
		[SYS_clone]             "sys_clone",
		[SYS_yield]             "sys_yield",
		[SYS_kill]              "sys_kill",
		[SYS_sleep]             "sys_sleep",
		[SYS_gettime]           "sys_gettime",
		[SYS_getpid]            "sys_getpid",
		[SYS_brk]               "sys_brk",
		[SYS_mmap]              "sys_mmap",
		[SYS_munmap]            "sys_munmap",
		[SYS_shmem]             "sys_shmem",
		[SYS_putc]              "sys_putc",
		[SYS_pgdir]             "sys_pgdir",
		[SYS_sem_init]          "sys_sem_init",
		[SYS_sem_post]          "sys_sem_post",
		[SYS_sem_wait]          "sys_sem_wait",
		[SYS_sem_free]          "sys_sem_free",
		[SYS_sem_get_value]     "sys_sem_get_value",
		[SYS_event_send]        "sys_event_send",
		[SYS_event_recv]        "sys_event_recv",
		[SYS_mbox_init]         "sys_mbox_init",
		[SYS_mbox_send]         "sys_mbox_send",
		[SYS_mbox_recv]         "sys_mbox_recv",
		[SYS_mbox_free]         "sys_mbox_free",
		[SYS_mbox_info]         "sys_mbox_info",
		[SYS_open]              "sys_open",
		[SYS_close]             "sys_close",
		[SYS_read]              "sys_read",
		[SYS_write]             "sys_write",
		[SYS_seek]              "sys_seek",
		[SYS_fstat]             "sys_fstat",
		[SYS_fsync]             "sys_fsync",
		[SYS_chdir]             "sys_chdir",
		[SYS_getcwd]            "sys_getcwd",
		[SYS_mkdir]             "sys_mkdir",
		[SYS_link]              "sys_link",
		[SYS_rename]            "sys_rename",
		[SYS_unlink]            "sys_unlink",
		[SYS_getdirentry]       "sys_getdirentry",
		[SYS_dup]               "sys_dup",
		[SYS_pipe]              "sys_pipe",
		[SYS_mkfifo]            "sys_mkfifo",
		[SYS_modify_ldt]		"sys_modify_ldt",
		[SYS_gettimeofday]		"sys_gettimeofday",
		[SYS_exit_group]		"sys_exit_group",
    };
#endif
	
    char c;

    int ret;

    switch (tf->tf_trapno) {
    case T_DEBUG:
    case T_BRKPT:
        debug_monitor(tf);
        break;
    case T_PGFLT:
        if ((ret = pgfault_handler(tf)) != 0) {
            print_trapframe(tf);
            if (current == NULL) {
                panic("handle pgfault failed. %e\n", ret);
            }
            else {
                if (trap_in_kernel(tf)) {
                    panic("handle pgfault failed in kernel mode. %e\n", ret);
                }
                cprintf("killed by kernel.\n");
                do_exit(-E_KILLED);
            }
        }
        break;
    case T_SYSCALL:
#ifdef DEBUG_PRINT_SYSCALL_NUM
		if (tf->tf_regs.reg_eax != SYS_write && 
				tf->tf_regs.reg_eax != SYS_read &&
				tf->tf_regs.reg_eax != SYS_putc)
			cprintf("Syscall [%d] (%s) detected!\n", tf->tf_regs.reg_eax, syscallnames[tf->tf_regs.reg_eax]);
#endif
        syscall();
        break;
    case IRQ_OFFSET + IRQ_TIMER:
        ticks ++;
        assert(current != NULL);
        run_timer_list();
        break;
    case IRQ_OFFSET + IRQ_COM1:
    case IRQ_OFFSET + IRQ_KBD:
        if ((c = cons_getc()) == 13) {
            debug_monitor(tf);
        }
        else {
            extern void dev_stdin_write(char c);
            dev_stdin_write(c);
        }
        break;
    case IRQ_OFFSET + IRQ_IDE1:
    case IRQ_OFFSET + IRQ_IDE2:
        /* do nothing */
        break;
    default:
        print_trapframe(tf);
        if (current != NULL) {
            cprintf("unhandled trap %d.\n", tf->tf_trapno);
            do_exit(-E_KILLED);
        }
        panic("unexpected trap in kernel.\n");
    }
}

void
trap(struct trapframe *tf) {
    // used for previous projects
    if (current == NULL) {
        trap_dispatch(tf);
    }
    else {
        // keep a trapframe chain in stack
        struct trapframe *otf = current->tf;
        current->tf = tf;

        bool in_kernel = trap_in_kernel(tf);

        trap_dispatch(tf);

        current->tf = otf;
        if (!in_kernel) {
            if (current->flags & PF_EXITING) {
				if (current->exit_code == -E_KILLED)
					do_exit(-E_KILLED);
				else
					do_exit(current->exit_code);
            }
            if (current->need_resched) {
                schedule();
            }
        }
    }
}

