#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <clock.h>
#include <assert.h>
#include <sem.h>
#include <event.h>
#include <mbox.h>
#include <stat.h>
#include <dirent.h>
#include <sysfile.h>

static int
sys_exit(uint32_t arg[]) {
	int error_code = (int)arg[0];
	return do_exit(error_code);
}

static int
sys_fork(uint32_t arg[]) {
	struct trapframe *tf = current->tf;
	uintptr_t stack = tf->tf_esp;
	return do_fork(0, stack, tf);
}

static int
sys_wait(uint32_t arg[]) {
	int pid = (int)arg[0];
	int *store = (int *)arg[1];
	return do_wait(pid, store);
}

static int
sys_exec(uint32_t arg[]) {
	const char *name = (const char *)arg[0];
	int argc = (int)arg[1];
	const char **argv = (const char **)arg[2];
	return do_execve(name, argc, argv);
}

static int
sys_clone(uint32_t arg[]) {
	struct trapframe *tf = current->tf;
	uint32_t clone_flags = (uint32_t)arg[0];
	uintptr_t stack = (uintptr_t)arg[1];
	if (stack == 0) {
		stack = tf->tf_esp;
	}
	return do_fork(clone_flags, stack, tf);
}

static int
sys_yield(uint32_t arg[]) {
	return do_yield();
}

static int
sys_sleep(uint32_t arg[]) {
	unsigned int time = (unsigned int)arg[0];
	return do_sleep(time);
}

static int
sys_kill(uint32_t arg[]) {
	int pid = (int)arg[0];
	return do_kill(pid);
}

static int
sys_gettime(uint32_t arg[]) {
	return (int)ticks;
}

static int
sys_getpid(uint32_t arg[]) {
	return current->pid;
}

static int
sys_brk(uint32_t arg[]) {
	uintptr_t *brk_store = (uintptr_t *)arg[0];
	return do_brk(brk_store);
}

static int
sys_mmap(uint32_t arg[]) {
	uintptr_t *addr_store = (uintptr_t *)arg[0];
	size_t len = (size_t)arg[1];
	uint32_t mmap_flags = (uint32_t)arg[2];
	return do_mmap(addr_store, len, mmap_flags);
}

static int
sys_munmap(uint32_t arg[]) {
	uintptr_t addr = (uint32_t)arg[0];
	size_t len = (size_t)arg[1];
	return do_munmap(addr, len);
}

static int
sys_shmem(uint32_t arg[]) {
	uintptr_t *addr_store = (uintptr_t *)arg[0];
	size_t len = (size_t)arg[1];
	uint32_t mmap_flags = (uint32_t)arg[2];
	return do_shmem(addr_store, len, mmap_flags);
}

static int
sys_putc(uint32_t arg[]) {
	int c = (int)arg[0];
	cputchar(c);
	return 0;
}

static int
sys_pgdir(uint32_t arg[]) {
	print_pgdir();
	return 0;
}

static int
sys_sem_init(uint32_t arg[]) {
	int value = (int)arg[0];
	return ipc_sem_init(value);
}

static int
sys_sem_post(uint32_t arg[]) {
	sem_t sem_id = (int)arg[0];
	return ipc_sem_post(sem_id);
}

static int
sys_sem_wait(uint32_t arg[]) {
	sem_t sem_id = (int)arg[0];
	unsigned int timeout = (unsigned int)arg[1];
	return ipc_sem_wait(sem_id, timeout);
}

static int
sys_sem_free(uint32_t arg[]) {
	sem_t sem_id = (int)arg[0];
	return ipc_sem_free(sem_id);
}

static int
sys_sem_get_value(uint32_t arg[]) {
	sem_t sem_id = (int)arg[0];
	int *value_store = (int *)arg[1];
	return ipc_sem_get_value(sem_id, value_store);
}

static int
sys_event_send(uint32_t arg[]) {
	int pid = (int)arg[0];
	int event = (int)arg[1];
	unsigned int timeout = (unsigned int)arg[2];
	return ipc_event_send(pid, event, timeout);
}

static int
sys_event_recv(uint32_t arg[]) {
	int *pid_store = (int *)arg[0];
	int *event_store = (int *)arg[1];
	unsigned int timeout = (unsigned int)arg[2];
	return ipc_event_recv(pid_store, event_store, timeout);
}

static int
sys_mbox_init(uint32_t arg[]) {
	unsigned int max_bytes = (unsigned int)arg[0];
	return ipc_mbox_init(max_bytes);
}

static int
sys_mbox_send(uint32_t arg[]) {
	int id = (int)arg[0];
	struct mboxbuf *buf = (struct mboxbuf *)arg[1];
	unsigned int timeout = (unsigned int)arg[2];
	return ipc_mbox_send(id, buf, timeout);
}

static int
sys_mbox_recv(uint32_t arg[]) {
	int id = (int)arg[0];
	struct mboxbuf *buf = (struct mboxbuf *)arg[1];
	unsigned int timeout = (unsigned int)arg[2];
	return ipc_mbox_recv(id, buf, timeout);
}

static int
sys_mbox_free(uint32_t arg[]) {
	int id = (int)arg[0];
	return ipc_mbox_free(id);
}

static int
sys_mbox_info(uint32_t arg[]) {
	int id = (int)arg[0];
	struct mboxinfo *info = (struct mboxinfo *)arg[1];
	return ipc_mbox_info(id, info);
}

static int
sys_open(uint32_t arg[]) {
	const char *path = (const char *)arg[0];
	uint32_t open_flags = (uint32_t)arg[1];
	return sysfile_open(path, open_flags);
}

static int
sys_close(uint32_t arg[]) {
	int fd = (int)arg[0];
	return sysfile_close(fd);
}

static int
sys_read(uint32_t arg[]) {
	int fd = (int)arg[0];
	void *base = (void *)arg[1];
	size_t len = (size_t)arg[2];
	return sysfile_read(fd, base, len);
}

static int
sys_write(uint32_t arg[]) {
	int fd = (int)arg[0];
	void *base = (void *)arg[1];
	size_t len = (size_t)arg[2];
	return sysfile_write(fd, base, len);
}

static int
sys_seek(uint32_t arg[]) {
	int fd = (int)arg[0];
	off_t pos = (off_t)arg[1];
	int whence = (int)arg[2];
	return sysfile_seek(fd, pos, whence);
}

static int
sys_fstat(uint32_t arg[]) {
	int fd = (int)arg[0];
	struct stat *stat = (struct stat *)arg[1];
	return sysfile_fstat(fd, stat);
}

static int
sys_fsync(uint32_t arg[]) {
	int fd = (int)arg[0];
	return sysfile_fsync(fd);
}

static int
sys_chdir(uint32_t arg[]) {
	const char *path = (const char *)arg[0];
	return sysfile_chdir(path);
}

static int
sys_getcwd(uint32_t arg[]) {
	char *buf = (char *)arg[0];
	size_t len = (size_t)arg[1];
	return sysfile_getcwd(buf, len);
}

static int
sys_mkdir(uint32_t arg[]) {
	const char *path = (const char *)arg[0];
	return sysfile_mkdir(path);
}

static int
sys_link(uint32_t arg[]) {
	const char *path1 = (const char *)arg[0];
	const char *path2 = (const char *)arg[1];
	return sysfile_link(path1, path2);
}

static int
sys_rename(uint32_t arg[]) {
	const char *path1 = (const char *)arg[0];
	const char *path2 = (const char *)arg[1];
	return sysfile_rename(path1, path2);
}

static int
sys_unlink(uint32_t arg[]) {
	const char *name = (const char *)arg[0];
	return sysfile_unlink(name);
}

static int
sys_getdirentry(uint32_t arg[]) {
	int fd = (int)arg[0];
	struct dirent *direntp = (struct dirent *)arg[1];
	return sysfile_getdirentry(fd, direntp);
}

static int
sys_dup(uint32_t arg[]) {
	int fd1 = (int)arg[0];
	int fd2 = (int)arg[1];
	return sysfile_dup(fd1, fd2);
}

static int
sys_pipe(uint32_t arg[]) {
	int *fd_store = (int *)arg[0];
	return sysfile_pipe(fd_store);
}

static int
sys_mkfifo(uint32_t arg[]) {
	const char *name = (const char *)arg[0];
	uint32_t open_flags = (uint32_t)arg[1];
	return sysfile_mkfifo(name, open_flags);
}

static int (*syscalls[])(uint32_t arg[]) = {
	[SYS_exit]				sys_exit,
	[SYS_fork]				sys_fork,
	[SYS_wait]				sys_wait,
	[SYS_exec]				sys_exec,
	[SYS_clone]				sys_clone,
	[SYS_yield]				sys_yield,
	[SYS_kill]				sys_kill,
	[SYS_sleep]				sys_sleep,
	[SYS_gettime]			sys_gettime,
	[SYS_getpid]			sys_getpid,
	[SYS_brk]				sys_brk,
	[SYS_mmap]				sys_mmap,
	[SYS_munmap]			sys_munmap,
	[SYS_shmem]				sys_shmem,
	[SYS_putc]				sys_putc,
	[SYS_pgdir]				sys_pgdir,
	[SYS_sem_init]			sys_sem_init,
	[SYS_sem_post]			sys_sem_post,
	[SYS_sem_wait]			sys_sem_wait,
	[SYS_sem_free]			sys_sem_free,
	[SYS_sem_get_value]		sys_sem_get_value,
	[SYS_event_send]		sys_event_send,
	[SYS_event_recv]		sys_event_recv,
	[SYS_mbox_init]			sys_mbox_init,
	[SYS_mbox_send]			sys_mbox_send,
	[SYS_mbox_recv]			sys_mbox_recv,
	[SYS_mbox_free]			sys_mbox_free,
	[SYS_mbox_info]			sys_mbox_info,
	[SYS_open]				sys_open,
	[SYS_close]				sys_close,
	[SYS_read]				sys_read,
	[SYS_write]				sys_write,
	[SYS_seek]				sys_seek,
	[SYS_fstat]				sys_fstat,
	[SYS_fsync]				sys_fsync,
	[SYS_chdir]				sys_chdir,
	[SYS_getcwd]			sys_getcwd,
	[SYS_mkdir]				sys_mkdir,
	[SYS_link]				sys_link,
	[SYS_rename]			sys_rename,
	[SYS_unlink]			sys_unlink,
	[SYS_getdirentry]		sys_getdirentry,
	[SYS_dup]				sys_dup,
	[SYS_pipe]				sys_pipe,
	[SYS_mkfifo]			sys_mkfifo,
};

#define NUM_SYSCALLS		((sizeof(syscalls)) / (sizeof(syscalls[0])))

void
syscall(void) {
	struct trapframe *tf = current->tf;
	uint32_t arg[5];
	int num = tf->tf_regs.reg_eax;
	if (num >= 0 && num < NUM_SYSCALLS) {
		if (syscalls[num] != NULL) {
			arg[0] = tf->tf_regs.reg_edx;
			arg[1] = tf->tf_regs.reg_ecx;
			arg[2] = tf->tf_regs.reg_ebx;
			arg[3] = tf->tf_regs.reg_edi;
			arg[4] = tf->tf_regs.reg_esi;
			tf->tf_regs.reg_eax = syscalls[num](arg);
			return ;
		}
	}
	print_trapframe(tf);
	panic("undefined syscall %d, pid = %d, name = %s.\n",
			num, current->pid, current->name);
}

