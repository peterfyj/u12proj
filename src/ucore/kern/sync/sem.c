#include <types.h>
#include <wait.h>
#include <atomic.h>
#include <slab.h>
#include <sem.h>
#include <vmm.h>
#include <ipc.h>
#include <proc.h>
#include <sync.h>
#include <assert.h>
#include <error.h>
#include <clock.h>

#define VALID_SEMID(sem_id)						\
	((uintptr_t)(sem_id) < (uintptr_t)(sem_id) + KERNBASE)

#define semid2sem(sem_id)						\
	((semaphore_t *)((uintptr_t)(sem_id) + KERNBASE))

#define sem2semid(sem)							\
	((sem_t)((uintptr_t)(sem) - KERNBASE))

void
sem_init(semaphore_t *sem, int value) {
	sem->value = value;
	sem->valid = 1;
	set_sem_count(sem, 0);
	wait_queue_init(&(sem->wait_queue));
}

static void __attribute__ ((noinline)) __up(semaphore_t *sem, uint32_t wait_state) {
	assert(sem->valid);
	bool intr_flag;
	local_intr_save(intr_flag);
	{
		wait_t *wait;
		if ((wait = wait_queue_first(&(sem->wait_queue))) == NULL) {
			sem->value ++;
		}
		else {
			assert(wait->proc->wait_state == wait_state);
			wakeup_wait(&(sem->wait_queue), wait, wait_state, 1);
		}
	}
	local_intr_restore(intr_flag);
}

static uint32_t __attribute__ ((noinline)) __down(semaphore_t *sem, uint32_t wait_state, timer_t *timer) {
	assert(sem->valid);
	bool intr_flag;
	local_intr_save(intr_flag);
	if (sem->value > 0) {
		sem->value --;
		local_intr_restore(intr_flag);
		return 0;
	}
	wait_t __wait, *wait = &__wait;
	wait_current_set(&(sem->wait_queue), wait, wait_state);
	ipc_add_timer(timer);
	local_intr_restore(intr_flag);

	schedule();

	local_intr_save(intr_flag);
	ipc_del_timer(timer);
	wait_current_del(&(sem->wait_queue), wait);
	local_intr_restore(intr_flag);

	if (wait->wakeup_flags != wait_state) {
		return wait->wakeup_flags;
	}
	return 0;
}

void
up(semaphore_t *sem) {
	__up(sem, WT_KSEM);
}

void
down(semaphore_t *sem) {
	uint32_t flags = __down(sem, WT_KSEM, NULL);
	assert(flags == 0);
}

bool
try_down(semaphore_t *sem) {
	bool intr_flag, ret = 0;
	local_intr_save(intr_flag);
	if (sem->value > 0) {
		sem->value --, ret = 1;
	}
	local_intr_restore(intr_flag);
	return ret;
}

static int
usem_up(semaphore_t *sem) {
	__up(sem, WT_USEM);
	return 0;
}

static int
usem_down(semaphore_t *sem, unsigned int timeout) {
	unsigned long saved_ticks;
	timer_t __timer, *timer = ipc_timer_init(timeout, &saved_ticks, &__timer);

	uint32_t flags;
	if ((flags = __down(sem, WT_USEM, timer)) == 0) {
		return 0;
	}
	assert(flags == WT_INTERRUPTED);
	return ipc_check_timeout(timeout, saved_ticks);
}

sem_queue_t *
sem_queue_create(void) {
	sem_queue_t *sem_queue;
	if ((sem_queue = kmalloc(sizeof(sem_queue_t))) != NULL) {
		sem_init(&(sem_queue->sem), 1);
		set_sem_queue_count(sem_queue, 0);
		list_init(&(sem_queue->semu_list));
	}
	return sem_queue;
}

void
sem_queue_destroy(sem_queue_t *sem_queue) {
	kfree(sem_queue);
}

sem_undo_t *
semu_create(semaphore_t *sem, int value) {
	sem_undo_t *semu;
	if ((semu = kmalloc(sizeof(sem_undo_t))) != NULL) {
		if (sem == NULL && (sem = kmalloc(sizeof(semaphore_t))) != NULL) {
			sem_init(sem, value);
		}
		if (sem != NULL) {
			sem_count_inc(sem);
			semu->sem = sem;
			return semu;
		}
		kfree(semu);
	}
	return NULL;
}

void
semu_destroy(sem_undo_t *semu) {
	if (sem_count_dec(semu->sem) == 0) {
		kfree(semu->sem);
	}
	kfree(semu);
}

int
dup_sem_queue(sem_queue_t *to, sem_queue_t *from) {
	assert(to != NULL && from != NULL);
	list_entry_t *list = &(from->semu_list), *le = list;
	while ((le = list_next(le)) != list) {
		sem_undo_t *semu = le2semu(le, semu_link);
		if (semu->sem->valid) {
			if ((semu = semu_create(semu->sem, 0)) == NULL) {
				return -E_NO_MEM;
			}
			list_add(&(to->semu_list), &(semu->semu_link));
		}
	}
	return 0;
}

void
exit_sem_queue(sem_queue_t *sem_queue) {
	assert(sem_queue != NULL && sem_queue_count(sem_queue) == 0);
	list_entry_t *list = &(sem_queue->semu_list), *le = list;
	while ((le = list_next(list)) != list) {
		list_del(le);
		semu_destroy(le2semu(le, semu_link));
	}
}

static sem_undo_t *
semu_list_search(list_entry_t *list, sem_t sem_id) {
	if (VALID_SEMID(sem_id)) {
		semaphore_t *sem = semid2sem(sem_id);
		list_entry_t *le = list;
		while ((le = list_next(le)) != list) {
			sem_undo_t *semu = le2semu(le, semu_link);
			if (semu->sem == sem) {
				list_del(le);
				if (sem->valid) {
					list_add_after(list, le);
					return semu;
				}
				else {
					semu_destroy(semu);
					return NULL;
				}
			}
		}
	}
	return NULL;
}

int
ipc_sem_init(int value) {
	assert(current->sem_queue != NULL);

	sem_undo_t *semu;
	if ((semu = semu_create(NULL, value)) == NULL) {
		return -E_NO_MEM;
	}

	sem_queue_t *sem_queue = current->sem_queue;
	down(&(sem_queue->sem));
	list_add_after(&(sem_queue->semu_list), &(semu->semu_link));
	up(&(sem_queue->sem));
	return sem2semid(semu->sem);
}

int
ipc_sem_post(sem_t sem_id) {
	assert(current->sem_queue != NULL);

	sem_undo_t *semu;
	sem_queue_t *sem_queue = current->sem_queue;
	down(&(sem_queue->sem));
	semu = semu_list_search(&(sem_queue->semu_list), sem_id);
	up(&(sem_queue->sem));
	if (semu != NULL) {
		return usem_up(semu->sem);
	}
	return -E_INVAL;
}

int
ipc_sem_wait(sem_t sem_id, unsigned int timeout) {
	assert(current->sem_queue != NULL);

	sem_undo_t *semu;
	sem_queue_t *sem_queue = current->sem_queue;
	down(&(sem_queue->sem));
	semu = semu_list_search(&(sem_queue->semu_list), sem_id);
	up(&(sem_queue->sem));
	if (semu != NULL) {
		return usem_down(semu->sem, timeout);
	}
	return -E_INVAL;
}

int
ipc_sem_free(sem_t sem_id) {
	assert(current->sem_queue != NULL);

	sem_undo_t *semu;
	sem_queue_t *sem_queue = current->sem_queue;
	down(&(sem_queue->sem));
	semu = semu_list_search(&(sem_queue->semu_list), sem_id);
	up(&(sem_queue->sem));

	int ret = -E_INVAL;
	if (semu != NULL) {
		bool intr_flag;
		local_intr_save(intr_flag);
		{
			semaphore_t *sem = semu->sem;
			sem->valid = 0, ret = 0;
			wakeup_queue(&(sem->wait_queue), WT_INTERRUPTED, 1);
		}
		local_intr_restore(intr_flag);
	}
	return ret;
}

int
ipc_sem_get_value(sem_t sem_id, int *value_store) {
	assert(current->sem_queue != NULL);
	if (value_store == NULL) {
		return -E_INVAL;
	}

	struct mm_struct *mm = current->mm;
	if (!user_mem_check(mm, (uintptr_t)value_store, sizeof(int), 1)) {
		return -E_INVAL;
	}

	sem_undo_t *semu;
	sem_queue_t *sem_queue = current->sem_queue;
	down(&(sem_queue->sem));
	semu = semu_list_search(&(sem_queue->semu_list), sem_id);
	up(&(sem_queue->sem));

	int ret = -E_INVAL;
	if (semu != NULL) {
		int value = semu->sem->value;
		lock_mm(mm);
		{
			if (copy_to_user(mm, value_store, &value, sizeof(int))) {
				ret = 0;
			}
		}
		unlock_mm(mm);
	}
	return ret;
}

