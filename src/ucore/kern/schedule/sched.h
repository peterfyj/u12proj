#ifndef __KERN_SCHEDULE_SCHED_H__
#define __KERN_SCHEDULE_SCHED_H__

#include <types.h>
#include <list.h>

struct proc_struct;

typedef struct {
	unsigned int expires;
	struct proc_struct *proc;
	list_entry_t timer_link;
} timer_t;

#define le2timer(le, member)			\
	to_struct((le), timer_t, member)

static inline timer_t *
timer_init(timer_t *timer, struct proc_struct *proc, int expires) {
	timer->expires = expires;
	timer->proc = proc;
	list_init(&(timer->timer_link));
	return timer;
}

struct run_queue;

struct sched_class {
	const char *name;
	void (*init)(struct run_queue *rq);
	void (*enqueue)(struct run_queue *rq, struct proc_struct *proc);
	void (*dequeue)(struct run_queue *rq, struct proc_struct *proc);
	struct proc_struct *(*pick_next)(struct run_queue *rq);
	void (*proc_tick)(struct run_queue *rq, struct proc_struct *proc);
};

struct run_queue {
	list_entry_t run_list;
	unsigned int proc_num;
	int max_time_slice;
	list_entry_t rq_link;
};

#define le2rq(le, member)			\
	to_struct((le), struct run_queue, member)

void sched_init(void);
void wakeup_proc(struct proc_struct *proc);
void schedule(void);
void add_timer(timer_t *timer);
void del_timer(timer_t *timer);
void run_timer_list(void);

#endif /* !__KERN_SCHEDULE_SCHED_H__ */

