#include <types.h>
#include <list.h>
#include <memlayout.h>
#include <assert.h>
#include <slab.h>
#include <sync.h>
#include <pmm.h>
#include <stdio.h>
#include <rb_tree.h>

#define BUFCTL_END      0xFFFFFFFFL
#define SLAB_LIMIT		0xFFFFFFFEL

typedef uint32_t kmem_bufctl_t;

typedef struct slab_s {
	list_entry_t slab_link;
	void *s_mem;
	size_t inuse;
	size_t offset;
	kmem_bufctl_t free;
} slab_t;

#define le2slab(le, member)					\
	to_struct((le), slab_t, member)

typedef struct kmem_cache_s kmem_cache_t;

struct kmem_cache_s {
	list_entry_t slabs_full;
	list_entry_t slabs_notfull;

	size_t objsize;
	size_t num;								// # of objs per slab
	size_t offset;
	bool off_slab;

	/* order of pages per slab (2^n) */
	size_t page_order;

	kmem_cache_t *slab_cachep;
};

#define MIN_SIZE_ORDER			5			// 32
#define MAX_SIZE_ORDER			17			// 128k
#define SLAB_CACHE_NUM			(MAX_SIZE_ORDER - MIN_SIZE_ORDER + 1)

static kmem_cache_t slab_cache[SLAB_CACHE_NUM];

static void init_kmem_cache(kmem_cache_t *cachep, size_t objsize, size_t align);
static void check_slab(void);

void
slab_init(void) {
	size_t i, align = 16;
	for (i = 0; i < SLAB_CACHE_NUM; i ++) {
		init_kmem_cache(slab_cache + i, 1 << (i + MIN_SIZE_ORDER), align);
	}
	check_slab();
}

size_t
slab_allocated(void) {
	size_t total = 0;
	int i;
	bool intr_flag;
	local_intr_save(intr_flag);
	{
		for (i = 0; i < SLAB_CACHE_NUM; i ++) {
			kmem_cache_t *cachep = slab_cache + i;
			list_entry_t *list, *le;
			list = le = &(cachep->slabs_full);
			while ((le = list_next(le)) != list) {
				total += cachep->num * cachep->objsize;
			}
			list = le = &(cachep->slabs_notfull);
			while ((le = list_next(le)) != list) {
				slab_t *slabp = le2slab(le, slab_link);
				total += slabp->inuse * cachep->objsize;
			}
		}
	}
	local_intr_restore(intr_flag);
	return total;
}

static size_t
slab_mgmt_size(size_t num, size_t align) {
	return ROUNDUP(sizeof(slab_t) + num * sizeof(kmem_bufctl_t), align);
}

static void
cache_estimate(size_t order, size_t objsize, size_t align, bool off_slab, size_t *remainder, size_t *num) {
	size_t nr_objs, mgmt_size;
	size_t slab_size = (PGSIZE << order);

	if (off_slab) {
		mgmt_size = 0;
		nr_objs = slab_size / objsize;
		if (nr_objs > SLAB_LIMIT) {
			nr_objs = SLAB_LIMIT;
		}
	}
	else {
		nr_objs = (slab_size - sizeof(slab_t)) / (objsize + sizeof(kmem_bufctl_t));
		while (slab_mgmt_size(nr_objs, align) + nr_objs * objsize > slab_size) {
			nr_objs --;
		}
		if (nr_objs > SLAB_LIMIT) {
			nr_objs = SLAB_LIMIT;
		}
		mgmt_size = slab_mgmt_size(nr_objs, align);
	}
	*num = nr_objs;
	*remainder = slab_size - nr_objs * objsize - mgmt_size;
}

static void
calculate_slab_order(kmem_cache_t *cachep, size_t objsize, size_t align, bool off_slab, size_t *left_over) {
	size_t order;
	for (order = 0; order <= KMALLOC_MAX_ORDER; order ++) {
		size_t num, remainder;
		cache_estimate(order, objsize, align, off_slab, &remainder, &num);
		if (num != 0) {
			if (off_slab) {
				size_t off_slab_limit = objsize - sizeof(slab_t);
				off_slab_limit /= sizeof(kmem_bufctl_t);
				if (num > off_slab_limit) {
					panic("off_slab: objsize = %d, num = %d.", objsize, num);
				}
			}
			if (remainder * 8 <= (PGSIZE << order)) {
				cachep->num = num;
				cachep->page_order = order;
				if (left_over != NULL) {
					*left_over = remainder;
				}
				return ;
			}
		}
	}
	panic("calculate_slab_over: failed.");
}

static inline size_t
getorder(size_t n) {
	size_t order = MIN_SIZE_ORDER, order_size = (1 << order);
	for (; order <= MAX_SIZE_ORDER; order ++, order_size <<= 1) {
		if (n <= order_size) {
			return order;
		}
	}
	panic("getorder failed. %d\n", n);
}

static void
init_kmem_cache(kmem_cache_t *cachep, size_t objsize, size_t align) {
	list_init(&(cachep->slabs_full));
	list_init(&(cachep->slabs_notfull));

	objsize = ROUNDUP(objsize, align);
	cachep->objsize = objsize;
	cachep->off_slab = (objsize >= (PGSIZE >> 3));

	size_t left_over;
	calculate_slab_order(cachep, objsize, align, cachep->off_slab, &left_over);

	assert(cachep->num > 0);

	size_t mgmt_size = slab_mgmt_size(cachep->num, align);

	if (cachep->off_slab && left_over >= mgmt_size) {
		cachep->off_slab = 0;
	}

	if (cachep->off_slab) {
		cachep->offset = 0;
		cachep->slab_cachep = slab_cache + (getorder(mgmt_size) - MIN_SIZE_ORDER);
	}
	else {
		cachep->offset = mgmt_size;
	}
}

static void *kmem_cache_alloc(kmem_cache_t *cachep);

#define slab_bufctl(slabp)				\
	((kmem_bufctl_t*)(((slab_t *)(slabp)) + 1))

static slab_t *
kmem_cache_slabmgmt(kmem_cache_t *cachep, struct Page *page) {
	void *objp = page2kva(page);
	slab_t *slabp;
	if (cachep->off_slab) {
		if ((slabp = kmem_cache_alloc(cachep->slab_cachep)) == NULL) {
			return NULL;
		}
	}
	else {
		slabp = page2kva(page);
	}
	slabp->inuse = 0;
	slabp->offset = cachep->offset;
	slabp->s_mem = objp + cachep->offset;
	return slabp;
}

#define SET_PAGE_CACHE(page, cachep) 												\
	do {																			\
		struct Page *__page = (struct Page *)(page);								\
		kmem_cache_t **__cachepp = (kmem_cache_t **)&(__page->page_link.next);		\
		*__cachepp = (kmem_cache_t *)(cachep);										\
	} while (0)

#define SET_PAGE_SLAB(page, slabp)	 												\
	do {																			\
		struct Page *__page = (struct Page *)(page);								\
		slab_t **__cachepp = (slab_t **)&(__page->page_link.prev);					\
		*__cachepp = (slab_t *)(slabp);												\
	} while (0)

static bool
kmem_cache_grow(kmem_cache_t *cachep) {
	struct Page *page = alloc_pages(1 << cachep->page_order);
	if (page == NULL) {
		goto failed;
	}

	slab_t *slabp;
	if ((slabp = kmem_cache_slabmgmt(cachep, page)) == NULL) {
		goto oops;
	}

	size_t order_size = (1 << cachep->page_order);
	do {
		SET_PAGE_CACHE(page, cachep);
		SET_PAGE_SLAB(page, slabp);
		SetPageSlab(page);
		page ++;
	} while (-- order_size);

	int i;
	for (i = 0; i < cachep->num; i ++) {
		slab_bufctl(slabp)[i] = i + 1;
	}
	slab_bufctl(slabp)[cachep->num - 1] = BUFCTL_END;
	slabp->free = 0;

	bool intr_flag;
	local_intr_save(intr_flag);
	{
		list_add(&(cachep->slabs_notfull), &(slabp->slab_link));
	}
	local_intr_restore(intr_flag);
	return 1;

oops:
	free_pages(page, 1 << cachep->page_order);
failed:
	return 0;
}

static void *
kmem_cache_alloc_one(kmem_cache_t *cachep, slab_t *slabp) {
	slabp->inuse ++;
	void *objp = slabp->s_mem + slabp->free * cachep->objsize;
	slabp->free = slab_bufctl(slabp)[slabp->free];

	if (slabp->free == BUFCTL_END) {
		list_del(&(slabp->slab_link));
		list_add(&(cachep->slabs_full), &(slabp->slab_link));
	}
	return objp;
}

static void *
kmem_cache_alloc(kmem_cache_t *cachep) {
	void *objp;
	bool intr_flag;

try_again:
	local_intr_save(intr_flag);
	if (list_empty(&(cachep->slabs_notfull))) {
		goto alloc_new_slab;
	}
	slab_t *slabp = le2slab(list_next(&(cachep->slabs_notfull)), slab_link);
	objp = kmem_cache_alloc_one(cachep, slabp);
	local_intr_restore(intr_flag);
	return objp;

alloc_new_slab:
	local_intr_restore(intr_flag);

	if (kmem_cache_grow(cachep)) {
		goto try_again;
	}
	return NULL;
}

void *
kmalloc(size_t size) {
	assert(size > 0);
	size_t order = getorder(size);
	if (order > MAX_SIZE_ORDER) {
		return NULL;
	}
	return kmem_cache_alloc(slab_cache + (order - MIN_SIZE_ORDER));
}

static void kmem_cache_free(kmem_cache_t *cachep, void *obj);

static void
kmem_slab_destroy(kmem_cache_t *cachep, slab_t *slabp) {
	struct Page *page = kva2page(slabp->s_mem - slabp->offset);

	struct Page *p = page;
	size_t order_size = (1 << cachep->page_order);
	do {
		assert(PageSlab(p));
		ClearPageSlab(p);
		p ++;
	} while (-- order_size);

	free_pages(page, 1 << cachep->page_order);

	if (cachep->off_slab) {
		kmem_cache_free(cachep->slab_cachep, slabp);
	}
}

static void
kmem_cache_free_one(kmem_cache_t *cachep, slab_t *slabp, void *objp) {
	size_t objnr = (objp - slabp->s_mem) / cachep->objsize;
	slab_bufctl(slabp)[objnr] = slabp->free;
	slabp->free = objnr;

	slabp->inuse --;

	if (slabp->inuse == 0) {
		list_del(&(slabp->slab_link));
		kmem_slab_destroy(cachep, slabp);
	}
	else if (slabp->inuse == cachep->num -1 ) {
		list_del(&(slabp->slab_link));
		list_add(&(cachep->slabs_notfull), &(slabp->slab_link));
	}
}

#define GET_PAGE_CACHE(page)								\
	(kmem_cache_t *)((page)->page_link.next)

#define GET_PAGE_SLAB(page) 								\
	(slab_t *)((page)->page_link.prev)

static void
kmem_cache_free(kmem_cache_t *cachep, void *objp) {
	bool intr_flag;
	struct Page *page = kva2page(objp);

	if (!PageSlab(page)) {
		panic("not a slab page %08x\n", objp);
	}
	local_intr_save(intr_flag);
	{
		kmem_cache_free_one(cachep, GET_PAGE_SLAB(page), objp);
	}
	local_intr_restore(intr_flag);
}

void
kfree(void *objp) {
	kmem_cache_free(GET_PAGE_CACHE(kva2page(objp)), objp);
}

static inline void
check_slab_empty(void) {
	int i;
	for (i = 0; i < SLAB_CACHE_NUM; i ++) {
		kmem_cache_t *cachep = slab_cache + i;
		assert(list_empty(&(cachep->slabs_full)));
		assert(list_empty(&(cachep->slabs_notfull)));
	}
}

void
check_slab(void) {
	int i;
	void *v0, *v1;

	size_t nr_free_pages_store = nr_free_pages();
	size_t slab_allocated_store = slab_allocated();

	/* slab must be empty now */
	check_slab_empty();
	assert(slab_allocated() == 0);

	kmem_cache_t *cachep0, *cachep1;

	cachep0 = slab_cache;
	assert(cachep0->objsize == 32 && cachep0->num > 1 && !cachep0->off_slab);
	assert((v0 = kmalloc(16)) != NULL);

	slab_t *slabp0, *slabp1;

	assert(!list_empty(&(cachep0->slabs_notfull)));
	slabp0 = le2slab(list_next(&(cachep0->slabs_notfull)), slab_link);
	assert(slabp0->inuse == 1 && list_next(&(slabp0->slab_link)) == &(cachep0->slabs_notfull));

	struct Page *p0, *p1;
	size_t order_size;


	p0 = kva2page(slabp0->s_mem - slabp0->offset), p1 = p0;
	order_size = (1 << cachep0->page_order);
	for (i = 0; i < cachep0->page_order; i ++, p1 ++) {
		assert(PageSlab(p1));
		assert(GET_PAGE_CACHE(p1) == cachep0 && GET_PAGE_SLAB(p1) == slabp0);
	}

	assert(v0 == slabp0->s_mem);
	assert((v1 = kmalloc(16)) != NULL && v1 == v0 + 32);

	kfree(v0);
	assert(slabp0->free == 0);
	kfree(v1);
	assert(list_empty(&(cachep0->slabs_notfull)));

	for (i = 0; i < cachep0->page_order; i ++, p0 ++) {
		assert(!PageSlab(p0));
	}


	v0 = kmalloc(16);
	assert(!list_empty(&(cachep0->slabs_notfull)));
	slabp0 = le2slab(list_next(&(cachep0->slabs_notfull)), slab_link);

	for (i = 0; i < cachep0->num - 1; i ++) {
		kmalloc(16);
	}

	assert(slabp0->inuse == cachep0->num);
	assert(list_next(&(cachep0->slabs_full)) == &(slabp0->slab_link));
	assert(list_empty(&(cachep0->slabs_notfull)));

	v1 = kmalloc(16);
	assert(!list_empty(&(cachep0->slabs_notfull)));
	slabp1 = le2slab(list_next(&(cachep0->slabs_notfull)), slab_link);

	kfree(v0);
	assert(list_empty(&(cachep0->slabs_full)));
	assert(list_next(&(slabp0->slab_link)) == &(slabp1->slab_link)
			|| list_next(&(slabp1->slab_link)) == &(slabp0->slab_link));

	kfree(v1);
	assert(!list_empty(&(cachep0->slabs_notfull)));
	assert(list_next(&(cachep0->slabs_notfull)) == &(slabp0->slab_link));
	assert(list_next(&(slabp0->slab_link)) == &(cachep0->slabs_notfull));

	v1 = kmalloc(16);
	assert(v1 == v0);
	assert(list_next(&(cachep0->slabs_full)) == &(slabp0->slab_link));
	assert(list_empty(&(cachep0->slabs_notfull)));

	for (i = 0; i < cachep0->num; i ++) {
		kfree(v1 + i * cachep0->objsize);
	}

	assert(list_empty(&(cachep0->slabs_full)));
	assert(list_empty(&(cachep0->slabs_notfull)));

	cachep0 = slab_cache;

	bool has_off_slab = 0;
	for (i = 0; i < SLAB_CACHE_NUM; i ++, cachep0 ++) {
		if (cachep0->off_slab) {
			has_off_slab = 1;
			cachep1 = cachep0->slab_cachep;
			if (!cachep1->off_slab) {
				break;
			}
		}
	}

	if (!has_off_slab) {
		goto check_pass;
	}

	assert(cachep0->off_slab && !cachep1->off_slab);
	assert(cachep1 < cachep0);

	assert(list_empty(&(cachep0->slabs_full)));
	assert(list_empty(&(cachep0->slabs_notfull)));

	assert(list_empty(&(cachep1->slabs_full)));
	assert(list_empty(&(cachep1->slabs_notfull)));

	v0 = kmalloc(cachep0->objsize);
	p0 = kva2page(v0);
	assert(page2kva(p0) == v0);

	if (cachep0->num == 1) {
		assert(!list_empty(&(cachep0->slabs_full)));
		slabp0 = le2slab(list_next(&(cachep0->slabs_full)), slab_link);
	}
	else {
		assert(!list_empty(&(cachep0->slabs_notfull)));
		slabp0 = le2slab(list_next(&(cachep0->slabs_notfull)), slab_link);
	}

	assert(slabp0 != NULL);

	if (cachep1->num == 1) {
		assert(!list_empty(&(cachep1->slabs_full)));
		slabp1 = le2slab(list_next(&(cachep1->slabs_full)), slab_link);
	}
	else {
		assert(!list_empty(&(cachep1->slabs_notfull)));
		slabp1 = le2slab(list_next(&(cachep1->slabs_notfull)), slab_link);
	}

	assert(slabp1 != NULL);

	order_size = (1 << cachep0->page_order);
	for (i = 0; i < order_size; i ++, p0 ++) {
		assert(PageSlab(p0));
		assert(GET_PAGE_CACHE(p0) == cachep0 && GET_PAGE_SLAB(p0) == slabp0);
	}

	kfree(v0);

check_pass:

	check_rb_tree();
	check_slab_empty();
	assert(slab_allocated() == 0);
	assert(nr_free_pages_store == nr_free_pages());
	assert(slab_allocated_store == slab_allocated());

	cprintf("check_slab() succeeded!\n");
}

