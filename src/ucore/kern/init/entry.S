#include <memlayout.h>
#include <mmu.h>

#define REALLOC(x) (x - KERNBASE)

.text

.globl kern_entry
kern_entry:
	# load temperate gdt
	lgdt REALLOC(__gdtdesc)
	movl $KERNEL_DS, %eax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss

	ljmp $KERNEL_CS, $relocated

relocated:

	# set ebp, esp
	movl $0x0, %ebp

	movl $bootstacktop, %esp

	call kern_init

# should never get here
spin:
	jmp spin

.data

.align PGSIZE
	.globl bootstack
bootstack:
	.space KSTACKSIZE
	.globl bootstacktop
bootstacktop:

.align 4
__gdt:
	SEG_NULL
	SEG_ASM(STA_X | STA_R, - KERNBASE, 0xFFFFFFFF)		# code segment
	SEG_ASM(STA_W, - KERNBASE, 0xFFFFFFFF)				# data segment
__gdtdesc:
	.word 0x17											# sizeof(__gdt) - 1
	.long REALLOC(__gdt)

