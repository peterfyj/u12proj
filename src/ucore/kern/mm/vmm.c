#include <vmm.h>
#include <sync.h>
#include <slab.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <pmm.h>
#include <x86.h>
#include <swap.h>
#include <shmem.h>
#include <proc.h>
#include <sem.h>

/* 
  vmm design include two parts: mm_struct (mm) & vma_struct (vma)
  mm is the memory manager for the set of continuous virtual memory  
  area which have the same PDT. vma is a continuous virtual memory area.
  There a linear link list for vma & a redblack link list for vma in mm.
---------------
  mm related functions:
   golbal functions
     struct mm_struct * mm_create(void)
     void mm_destroy(struct mm_struct *mm)
     int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr)
--------------
  vma related functions:
   global functions
     struct vma_struct * vma_create (uintptr_t vm_start, uintptr_t vm_end,...)
     void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
     struct vma_struct * find_vma(struct mm_struct *mm, uintptr_t addr)
   local functions
     inline void check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
     inline struct vma_struct * find_vma_rb(rb_tree *tree, uintptr_t addr) 
     inline void insert_vma_rb(rb_tree *tree, struct vma_struct *vma, ....
     inline int vma_compare(rb_node *node1, rb_node *node2)
---------------
   check correctness functions
     void check_vmm(void);
     void check_vma_struct(void);
     void check_pgfault(void);
*/

static void check_vmm(void);
static void check_vma_struct(void);
static void check_pgfault(void);

void
lock_mm(struct mm_struct *mm) {
    if (mm != NULL) {
        down(&(mm->mm_sem));
        if (current != NULL) {
            mm->locked_by = current->pid;
        }
    }
}

void
unlock_mm(struct mm_struct *mm) {
    if (mm != NULL) {
        up(&(mm->mm_sem));
        mm->locked_by = 0;
    }
}

bool
try_lock_mm(struct mm_struct *mm) {
    if (mm != NULL) {
        if (!try_down(&(mm->mm_sem))) {
            return 0;
        }
        if (current != NULL) {
            mm->locked_by = current->pid;
        }
    }
    return 1;
}

// mm_create -  alloc a mm_struct & initialize it.
struct mm_struct *
mm_create(void) {
    struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));
    if (mm != NULL) {
        list_init(&(mm->mmap_list));
        mm->mmap_tree = NULL;
        mm->mmap_cache = NULL;
        mm->pgdir = NULL;
        mm->map_count = 0;
        mm->swap_address = 0;
        set_mm_count(mm, 0);
        mm->locked_by = 0;
        mm->brk_start = mm->brk = 0;
        list_init(&(mm->proc_mm_link));
        sem_init(&(mm->mm_sem), 1);
    }
    return mm;
}

// vma_create - alloc a vma_struct & initialize it. (addr range: vm_start~vm_end)
struct vma_struct *
vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags) {
    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct));
    if (vma != NULL) {
        vma->vm_start = vm_start;
        vma->vm_end = vm_end;
        vma->vm_flags = vm_flags;
        vma->shmem = NULL;
        vma->shmem_off = 0;
    }
    return vma;
}

// vma_destroy - free vma_struct
static void
vma_destroy(struct vma_struct *vma) {
    if (vma->vm_flags & VM_SHARE) {
        if (shmem_ref_dec(vma->shmem) == 0) {
            shmem_destroy(vma->shmem);
        }
    }
    kfree(vma);
}

// find_vma_rb - find a vma  (vma->vm_start <= addr <= vma_vm_end) in rb tree
static inline struct vma_struct *
find_vma_rb(rb_tree *tree, uintptr_t addr) {
    rb_node *node = rb_node_root(tree);
    struct vma_struct *vma = NULL, *tmp;
    while (node != NULL) {
        tmp = rbn2vma(node, rb_link);
        if (tmp->vm_end > addr) {
            vma = tmp;
            if (tmp->vm_start <= addr) {
                break;
            }
            node = rb_node_left(tree, node);
        }
        else {
            node = rb_node_right(tree, node);
        }
    }
    return vma;
}

// find_vma - find a vma  (vma->vm_start <= addr <= vma_vm_end)
struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr) {
    struct vma_struct *vma = NULL;
    if (mm != NULL) {
        vma = mm->mmap_cache;
        if (!(vma != NULL && vma->vm_start <= addr && vma->vm_end > addr)) {
            if (mm->mmap_tree != NULL) {
                vma = find_vma_rb(mm->mmap_tree, addr);
            }
            else {
                bool found = 0;
                list_entry_t *list = &(mm->mmap_list), *le = list;
                while ((le = list_next(le)) != list) {
                    vma = le2vma(le, list_link);
                    if (addr < vma->vm_end) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    vma = NULL;
                }
            }
        }
        if (vma != NULL) {
            mm->mmap_cache = vma;
        }
    }
    return vma;
}

struct vma_struct *
find_vma_intersection(struct mm_struct *mm, uintptr_t start, uintptr_t end) {
    struct vma_struct *vma = find_vma(mm, start);
    if (vma != NULL && end <= vma->vm_start) {
        vma = NULL;
    }
    return vma;
}

// vma_compare - compare vma1->vm_start < vma2->vm_start ?
static inline int
vma_compare(rb_node *node1, rb_node *node2) {
    struct vma_struct *vma1 = rbn2vma(node1, rb_link);
    struct vma_struct *vma2 = rbn2vma(node2, rb_link);
    uintptr_t start1 = vma1->vm_start, start2 = vma2->vm_start;
    return (start1 < start2) ? -1 : (start1 > start2) ? 1 : 0;
}

// check_vma_overlap - check if vma1 overlaps vma2 ?
static inline void
check_vma_overlap(struct vma_struct *prev, struct vma_struct *next) {
    assert(prev->vm_start < prev->vm_end);
    assert(prev->vm_end <= next->vm_start);
    assert(next->vm_start < next->vm_end);
}

// insert_vma_rb - insert vma in rb tree according vma->start_addr
static inline void
insert_vma_rb(rb_tree *tree, struct vma_struct *vma, struct vma_struct **vma_prevp) {
    rb_node *node = &(vma->rb_link), *prev;
    rb_insert(tree, node);
    if (vma_prevp != NULL) {
        prev = rb_node_prev(tree, node);
        *vma_prevp = (prev != NULL) ? rbn2vma(prev, rb_link) : NULL;
    }
}

// insert_vma_struct -insert vma in mm's rb tree link & list link
void
insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma) {
    assert(vma->vm_start < vma->vm_end);
    list_entry_t *list = &(mm->mmap_list);
    list_entry_t *le_prev = list, *le_next;
    if (mm->mmap_tree != NULL) {
        struct vma_struct *mmap_prev;
        insert_vma_rb(mm->mmap_tree, vma, &mmap_prev);
        if (mmap_prev != NULL) {
            le_prev = &(mmap_prev->list_link);
        }
    }
    else {
        list_entry_t *le = list;
        while ((le = list_next(le)) != list) {
            struct vma_struct *mmap_prev = le2vma(le, list_link);
            if (mmap_prev->vm_start > vma->vm_start) {
                break;
            }
            le_prev = le;
        }
    }

    le_next = list_next(le_prev);

    /* check overlap */
    if (le_prev != list) {
        check_vma_overlap(le2vma(le_prev, list_link), vma);
    }
    if (le_next != list) {
        check_vma_overlap(vma, le2vma(le_next, list_link));
    }

    vma->vm_mm = mm;
    list_add_after(le_prev, &(vma->list_link));

    mm->map_count ++;
    if (mm->mmap_tree == NULL && mm->map_count >= RB_MIN_MAP_COUNT) {

        /* try to build red-black tree now, but may fail. */
        mm->mmap_tree = rb_tree_create(vma_compare);

        if (mm->mmap_tree != NULL) {
            list_entry_t *list = &(mm->mmap_list), *le = list;
            while ((le = list_next(le)) != list) {
                insert_vma_rb(mm->mmap_tree, le2vma(le, list_link), NULL);
            }
        }
    }
}

// remove_vma_struct - remove vma from mm's rb tree link & list link
static int
remove_vma_struct(struct mm_struct *mm, struct vma_struct *vma) {
    assert(mm == vma->vm_mm);
    if (mm->mmap_tree != NULL) {
        rb_delete(mm->mmap_tree, &(vma->rb_link));
    }
    list_del(&(vma->list_link));
    if (vma == mm->mmap_cache) {
        mm->mmap_cache = NULL;
    }
    mm->map_count --;
    return 0;
}

// mm_destroy - free mm and mm internal fields
void
mm_destroy(struct mm_struct *mm) {
    assert(mm_count(mm) == 0);
    if (mm->mmap_tree != NULL) {
        rb_tree_destroy(mm->mmap_tree);
    }
    list_entry_t *list = &(mm->mmap_list), *le;
    while ((le = list_next(list)) != list) {
        list_del(le);
        vma_destroy(le2vma(le, list_link));
    }
    kfree(mm);
}

// vmm_init - initialize virtual memory management
//          - now just call check_vmm to check correctness of vmm
void
vmm_init(void) {
    check_vmm();
}

int
mm_map(struct mm_struct *mm, uintptr_t addr, size_t len, uint32_t vm_flags,
        struct vma_struct **vma_store) {
    uintptr_t start = ROUNDDOWN(addr, PGSIZE), end = ROUNDUP(addr + len, PGSIZE);
    if (!USER_ACCESS(start, end)) {
        return -E_INVAL;
    }

    assert(mm != NULL);

    int ret = -E_INVAL;

    struct vma_struct *vma;
    if ((vma = find_vma(mm, start)) != NULL && end > vma->vm_start) {
        goto out;
    }
    ret = -E_NO_MEM;
    vm_flags &= ~VM_SHARE;
    if ((vma = vma_create(start, end, vm_flags)) == NULL) {
        goto out;
    }
    insert_vma_struct(mm, vma);
    if (vma_store != NULL) {
        *vma_store = vma;
    }
    ret = 0;

out:
    return ret;
}

int
mm_map_shmem(struct mm_struct *mm, uintptr_t addr, uint32_t vm_flags,
        struct shmem_struct *shmem, struct vma_struct **vma_store) {
    if ((addr % PGSIZE) != 0 || shmem == NULL) {
        return -E_INVAL;
    }
    int ret;
    struct vma_struct *vma;
    shmem_ref_inc(shmem);
    if ((ret = mm_map(mm, addr, shmem->len, vm_flags, &vma)) != 0) {
        shmem_ref_dec(shmem);
        return ret;
    }
    vma->shmem = shmem;
    vma->shmem_off = 0;
    vma->vm_flags |= VM_SHARE;
    if (vma_store != NULL) {
        *vma_store = vma;
    }
    return 0;
}

static void
vma_resize(struct vma_struct *vma, uintptr_t start, uintptr_t end) {
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(vma->vm_start <= start && start < end && end <= vma->vm_end);
    if (vma->vm_flags & VM_SHARE) {
        vma->shmem_off += start - vma->vm_start;
    }
    vma->vm_start = start, vma->vm_end = end;
}

int
mm_unmap(struct mm_struct *mm, uintptr_t addr, size_t len) {
    uintptr_t start = ROUNDDOWN(addr, PGSIZE), end = ROUNDUP(addr + len, PGSIZE);
    if (!USER_ACCESS(start, end)) {
        return -E_INVAL;
    }

    assert(mm != NULL);

    struct vma_struct *vma;
    if ((vma = find_vma(mm, start)) == NULL || end <= vma->vm_start) {
        return 0;
    }

    if (vma->vm_start < start && end < vma->vm_end) {
        struct vma_struct *nvma;
        if ((nvma = vma_create(vma->vm_start, start, vma->vm_flags)) == NULL) {
            return -E_NO_MEM;
        }
        vma_resize(vma, end, vma->vm_end);
        insert_vma_struct(mm, nvma);
        unmap_range(mm->pgdir, start, end);
        return 0;
    }

    list_entry_t free_list, *le;
    list_init(&free_list);
    while (vma->vm_start < end) {
        le = list_next(&(vma->list_link));
        remove_vma_struct(mm, vma);
        list_add(&free_list, &(vma->list_link));
        if (le == &(mm->mmap_list)) {
            break;
        }
        vma = le2vma(le, list_link);
    }

    le = list_next(&free_list);
    while (le != &free_list) {
        vma = le2vma(le, list_link);
        le = list_next(le);
        uintptr_t un_start, un_end;
        if (vma->vm_start < start) {
            un_start = start, un_end = vma->vm_end;
            vma_resize(vma, vma->vm_start, un_start);
            insert_vma_struct(mm, vma);
        }
        else {
            un_start = vma->vm_start, un_end = vma->vm_end;
            if (end < un_end) {
                un_end = end;
                vma_resize(vma, un_end, vma->vm_end);
                insert_vma_struct(mm, vma);
            }
            else {
                vma_destroy(vma);
            }
        }
        unmap_range(mm->pgdir, un_start, un_end);
    }
    return 0;
}

int
dup_mmap(struct mm_struct *to, struct mm_struct *from) {
    assert(to != NULL && from != NULL);
    list_entry_t *list = &(from->mmap_list), *le = list;
    while ((le = list_prev(le)) != list) {
        struct vma_struct *vma, *nvma;
        vma = le2vma(le, list_link);
        nvma = vma_create(vma->vm_start, vma->vm_end, vma->vm_flags);
        if (nvma == NULL) {
            return -E_NO_MEM;
        }
        else {
            if (vma->vm_flags & VM_SHARE) {
                nvma->shmem = vma->shmem;
                nvma->shmem_off = vma->shmem_off;
                shmem_ref_inc(vma->shmem);
            }
        }
        insert_vma_struct(to, nvma);
        bool share = (vma->vm_flags & VM_SHARE);
        if (copy_range(to->pgdir, from->pgdir, vma->vm_start, vma->vm_end, share) != 0) {
            return -E_NO_MEM;
        }
    }
    return 0;
}

void
exit_mmap(struct mm_struct *mm) {
    assert(mm != NULL && mm_count(mm) == 0);
    pde_t *pgdir = mm->pgdir;
    list_entry_t *list = &(mm->mmap_list), *le = list;
    while ((le = list_next(le)) != list) {
        struct vma_struct *vma = le2vma(le, list_link);
        unmap_range(pgdir, vma->vm_start, vma->vm_end);
    }
    while ((le = list_next(le)) != list) {
        struct vma_struct *vma = le2vma(le, list_link);
        exit_range(pgdir, vma->vm_start, vma->vm_end);
    }
}

/**
 * The standard get_unmapped_area() will check from the tail
 * of the vma link. This is not we want because in GO this is
 * likely to make big fragment when GO is requesting 768MB reserved
 * area.  get_unmapped_area_with_hint() will check from the specific
 * address addr, all the way up to USERTOP.
 * The vma link is like this:
 * (HDR)<->(vma)<->(vma)<->(vma)<->...<->(vma)<--
 *   ^                                          |
 *   |                                          |
 *   --------------------------------------------
 * Where (HDR) refers to mm->mmap_list.
 *
 * [!]Note: Please use this ONLY when addr is sure to be included in an
 * existing vma! Otherwise the function would return 0.
 */
uintptr_t
get_unmapped_area_with_hint(struct mm_struct *mm, uintptr_t addr, size_t len) {
	
	if (len == 0 || len > USERTOP) {
		return 0;
	}
	
	struct vma_struct *vma = find_vma(mm, addr);	// First find which vma includes addr;
	struct vma_struct *vma_next;					// For the checking loop;
	if (vma == NULL) {
		return 0;
	}
	
	list_entry_t *list = &(mm->mmap_list);
	list_entry_t *le = &(vma->list_link);
	
	do {
		vma = le2vma(le, list_link);				// Redundant for the init vma;
		if (vma->vm_end + len > USERTOP) {			// Reaching USERTOP;
			return 0;
		}
		if (list_next(le) == list) {				// Which means we can place it after the last one;
			return vma->vm_end;
		} else {									// Check whether it can placed right after le;
			vma_next = le2vma(list_next(le), list_link);
			if (vma->vm_end + len <= vma_next->vm_start) {
				return vma->vm_end;
			}
		}
	} while ((le = list_next(le)) != list);
	
	return 0;										// Fail;
}

uintptr_t
get_unmapped_area(struct mm_struct *mm, size_t len) {
    if (len == 0 || len > USERTOP) {
        return 0;
    }
    uintptr_t start = USERTOP - len;
    list_entry_t *list = &(mm->mmap_list), *le = list;
    while ((le = list_prev(le)) != list) {
        struct vma_struct *vma = le2vma(le, list_link);
        if (start >= vma->vm_end) {
            return start;
        }
        if (start + len > vma->vm_start) {
            if (len >= vma->vm_start) {
                return 0;
            }
            start = vma->vm_start - len;
        }
    }
    return (start >= USERBASE) ? start : 0;
}

int
mm_brk(struct mm_struct *mm, uintptr_t addr, size_t len) {
    uintptr_t start = ROUNDDOWN(addr, PGSIZE), end = ROUNDUP(addr + len, PGSIZE);
    if (!USER_ACCESS(start, end)) {
        return -E_INVAL;
    }

    int ret;
    if ((ret = mm_unmap(mm, start, end - start)) != 0) {
        return ret;
    }
    uint32_t vm_flags = VM_READ | VM_WRITE;
    struct vma_struct *vma = find_vma(mm, start - 1);
    if (vma != NULL && vma->vm_end == start && vma->vm_flags == vm_flags) {
        vma->vm_end = end;
        return 0;
    }
    if ((vma = vma_create(start, end, vm_flags)) == NULL) {
        return -E_NO_MEM;
    }
    insert_vma_struct(mm, vma);
    return 0;
}

bool
user_mem_check(struct mm_struct *mm, uintptr_t addr, size_t len, bool write) {
    if (mm != NULL) {
        if (!USER_ACCESS(addr, addr + len)) {
            return 0;
        }
        struct vma_struct *vma;
        uintptr_t start = addr, end = addr + len;
        while (start < end) {
            if ((vma = find_vma(mm, start)) == NULL || start < vma->vm_start) {
                return 0;
            }
            if (!(vma->vm_flags & ((write) ? VM_WRITE : VM_READ))) {
                return 0;
            }
            if (write && (vma->vm_flags & VM_STACK)) {
                if (start < vma->vm_start + PGSIZE) {
                    return 0;
                }
            }
            start = vma->vm_end;
        }
        return 1;
    }
    return KERN_ACCESS(addr, addr + len);
}

bool
copy_from_user(struct mm_struct *mm, void *dst, const void *src, size_t len, bool writable) {
    if (!user_mem_check(mm, (uintptr_t)src, len, writable)) {
        return 0;
    }
    memcpy(dst, src, len);
    return 1;
}

bool
copy_to_user(struct mm_struct *mm, void *dst, const void *src, size_t len) {
    if (!user_mem_check(mm, (uintptr_t)dst, len, 1)) {
        return 0;
    }
    memcpy(dst, src, len);
    return 1;
}

bool
copy_string(struct mm_struct *mm, char *dst, const char *src, size_t maxn) {
    size_t alen, part = ROUNDDOWN((uintptr_t)src + PGSIZE, PGSIZE) - (uintptr_t)src;
    while (1) {
        if (part > maxn) {
            part = maxn;
        }
        if (!user_mem_check(mm, (uintptr_t)src, part, 0)) {
            return 0;
        }
        if ((alen = strnlen(src, part)) < part) {
            memcpy(dst, src, alen + 1);
            return 1;
        }
        if (part == maxn) {
            return 0;
        }
        memcpy(dst, src, part);
        dst += part, src += part, maxn -= part;
        part = PGSIZE;
    }
}

// check_vmm - check correctness of vmm
static void
check_vmm(void) {
    size_t nr_free_pages_store = nr_free_pages();
    size_t slab_allocated_store = slab_allocated();

    check_vma_struct();
    check_pgfault();

    assert(nr_free_pages_store == nr_free_pages());
    assert(slab_allocated_store == slab_allocated());

    cprintf("check_vmm() succeeded.\n");
}

static void
check_vma_struct(void) {
    size_t nr_free_pages_store = nr_free_pages();
    size_t slab_allocated_store = slab_allocated();

    struct mm_struct *mm = mm_create();
    assert(mm != NULL);

    int step1 = RB_MIN_MAP_COUNT * 2, step2 = step1 * 10;

    int i;
    for (i = step1; i >= 0; i --) {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    for (i = step1 + 1; i <= step2; i ++) {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    list_entry_t *le = list_next(&(mm->mmap_list));

    for (i = 0; i <= step2; i ++) {
        assert(le != &(mm->mmap_list));
        struct vma_struct *mmap = le2vma(le, list_link);
        assert(mmap->vm_start == i * 5 && mmap->vm_end == i * 5 + 2);
        le = list_next(le);
    }

    for (i = 0; i < 5 * step2 + 2; i ++) {
        struct vma_struct *vma = find_vma(mm, i);
        assert(vma != NULL);
        int j = i / 5;
        if (i >= 5 * j + 2) {
            j ++;
        }
        assert(vma->vm_start == j * 5 && vma->vm_end == j * 5 + 2);
    }

    mm_destroy(mm);

    assert(nr_free_pages_store == nr_free_pages());
    assert(slab_allocated_store == slab_allocated());

    cprintf("check_vma_struct() succeeded!\n");
}

struct mm_struct *check_mm_struct;

// check_pgfault - check correctness of pgfault handler
static void
check_pgfault(void) {
    size_t nr_free_pages_store = nr_free_pages();
    size_t slab_allocated_store = slab_allocated();

    check_mm_struct = mm_create();
    assert(check_mm_struct != NULL);

    struct mm_struct *mm = check_mm_struct;
    pde_t *pgdir = mm->pgdir = boot_pgdir;
    assert(pgdir[0] == 0);

    struct vma_struct *vma = vma_create(0, PTSIZE, VM_WRITE);
    assert(vma != NULL);

    insert_vma_struct(mm, vma);

    uintptr_t addr = 0x100;
    assert(find_vma(mm, addr) == vma);

    int i, sum = 0;
    for (i = 0; i < 100; i ++) {
        *(char *)(addr + i) = i;
        sum += i;
    }
    for (i = 0; i < 100; i ++) {
        sum -= *(char *)(addr + i);
    }
    assert(sum == 0);

    page_remove(pgdir, ROUNDDOWN(addr, PGSIZE));
    free_page(pa2page(pgdir[0]));
    pgdir[0] = 0;

    mm->pgdir = NULL;
    mm_destroy(mm);
    check_mm_struct = NULL;

    assert(nr_free_pages_store == nr_free_pages());
    assert(slab_allocated_store == slab_allocated());

    cprintf("check_pgfault() succeeded!\n");
}

// do_pgfault - interrupt handler to process the page fault execption
int
do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr) {
    if (mm == NULL) {
        assert(current != NULL);
        panic("page fault in kernel thread: pid = %d, %d %08x.\n",
                current->pid, error_code, addr);
    }

    bool need_unlock = 1;
    if (!try_lock_mm(mm)) {
        if (current != NULL && mm->locked_by == current->pid) {
            need_unlock = 0;
        }
        else {
            lock_mm(mm);
        }
    }

    int ret = -E_INVAL;
    struct vma_struct *vma = find_vma(mm, addr);
    if (vma == NULL || vma->vm_start > addr) {
        goto failed;
    }
    if (vma->vm_flags & VM_STACK) {
        if (addr < vma->vm_start + PGSIZE) {
            goto failed;
        }
    }

    switch (error_code & 3) {
    default:
            /* default is 3: write, present */
    case 2: /* write, not present */
        if (!(vma->vm_flags & VM_WRITE)) {
            goto failed;
        }
        break;
    case 1: /* read, present */
        goto failed;
    case 0: /* read, not present */
        if (!(vma->vm_flags & (VM_READ | VM_EXEC))) {
            goto failed;
        }
    }

    uint32_t perm = PTE_U;
    if (vma->vm_flags & VM_WRITE) {
        perm |= PTE_W;
    }
    addr = ROUNDDOWN(addr, PGSIZE);

    ret = -E_NO_MEM;
    pte_t *ptep;

    if ((ptep = get_pte(mm->pgdir, addr, 1)) == NULL) {
        goto failed;
    }
    if (*ptep == 0) {
        if (!(vma->vm_flags & VM_SHARE)) {
            if (pgdir_alloc_page(mm->pgdir, addr, perm) == NULL) {
                goto failed;
            }
        }
        else {
            lock_shmem(vma->shmem);
            uintptr_t shmem_addr = addr - vma->vm_start + vma->shmem_off;
            pte_t *sh_ptep = shmem_get_entry(vma->shmem, shmem_addr, 1);
            if (sh_ptep == NULL || *sh_ptep == 0) {
                unlock_shmem(vma->shmem);
                goto failed;
            }
            unlock_shmem(vma->shmem);
            if (*sh_ptep & PTE_P) {
                page_insert(mm->pgdir, pa2page(*sh_ptep), addr, perm);
            }
            else {
                swap_duplicate(*ptep);
                *ptep = *sh_ptep;
            }
        }
    }
    else {
        struct Page *page, *newpage = NULL;
        bool cow = ((vma->vm_flags & (VM_SHARE | VM_WRITE)) == VM_WRITE), may_copy = 1;

        assert(!(*ptep & PTE_P) || ((error_code & 2) && !(*ptep & PTE_W) && cow));
        if (cow) {
            newpage = alloc_page();
        }
        if (*ptep & PTE_P) {
            page = pte2page(*ptep);
        }
        else {
            if ((ret = swap_in_page(*ptep, &page)) != 0) {
                if (newpage != NULL) {
                    free_page(newpage);
                }
                goto failed;
            }
            if (!(error_code & 2) && cow) {
                perm &= ~PTE_W;
                may_copy = 0;
            }
        }

        if (cow && may_copy) {
            if (page_ref(page) + swap_page_count(page) > 1) {
                if (newpage == NULL) {
                    goto failed;
                }
                memcpy(page2kva(newpage), page2kva(page), PGSIZE);
                page = newpage, newpage = NULL;
            }
        }
        page_insert(mm->pgdir, page, addr, perm);
        if (newpage != NULL) {
            free_page(newpage);
        }
    }
    ret = 0;

failed:
    if (need_unlock) {
        unlock_mm(mm);
    }
    return ret;
}

