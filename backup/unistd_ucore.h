#ifndef __LIBS_UNISTD_H__
#define __LIBS_UNISTD_H__

/* syscall number */
#define SYS_exit			1
#define SYS_fork			2
#define SYS_wait			3
#define SYS_exec			4
#define SYS_clone			5
#define SYS_vfork			8
#define SYS_yield			10
#define SYS_sleep			11
#define SYS_kill			12
#define SYS_gettime			17
#define SYS_getpid			18
#define SYS_brk				19
#define SYS_mmap			20
#define SYS_munmap			21
#define SYS_shmem			22
#define SYS_putc			30
#define SYS_pgdir			31
#define SYS_sem_init		40
#define SYS_sem_post		41
#define SYS_sem_wait		42
#define SYS_sem_free		43
#define SYS_sem_get_value	44
#define SYS_event_send		48
#define SYS_event_recv		49
#define SYS_mbox_init		50
#define SYS_mbox_send		51
#define SYS_mbox_recv		52
#define SYS_mbox_free		53
#define SYS_mbox_info		54
#define SYS_open			100
#define SYS_close			101
#define SYS_read			102
#define SYS_write			103
#define SYS_seek			104
#define SYS_fstat			110
#define SYS_fsync			111
#define SYS_chdir			120
#define SYS_getcwd			121
#define SYS_mkdir			122
#define SYS_link			123
#define SYS_rename			124
#define SYS_readlink		125
#define SYS_symlink			126
#define SYS_unlink			127
#define SYS_getdirentry		128
#define SYS_dup				130
#define SYS_pipe			140
#define SYS_mkfifo			141

#endif /* !__LIBS_UNISTD_H__ */

