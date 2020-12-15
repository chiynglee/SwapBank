#include "common.h"
#include "shalloc.h"

#include <linux/mmzone.h>
#include <linux/vmstat.h>

static unsigned int init_shcomp_free_page(struct shcomp_area_info *sharea);
static struct page *get_page_from_vaddr(void *va);
static inline void list_del_entry(struct list_head *entry);


static unsigned int area_was_used = 0;
struct shcomp_area_info *sharea_info;


static inline void list_del_entry(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->prev = NULL;
	entry->next = NULL;	
}

int shcomp_is_ready(void)
{
	return gntpage_is_ready();
}

void *init_shcomp_area(void)
{
	unsigned int cnt;
	struct shcomp_area_info *sharea;

	if(area_was_used != 0) {
		printk("shcomp_init: shared memory was already used\n");
		return NULL;
	}

	if(!shcomp_is_ready()) {
		printk("shcomp_init: shared memory is not ready\n");
		return NULL;
	}

	sharea = (struct shcomp_area_info *)kzalloc(sizeof(struct shcomp_area_info),  GFP_NOIO | __GFP_HIGH);
	if(!sharea) {
		printk("shcomp_init: information area for a shared memory is not allocated\n");
		return NULL;
	}
	
	INIT_LIST_HEAD(&sharea->free_area);
	INIT_LIST_HEAD(&sharea->used);
	
	cnt = init_shcomp_free_page(sharea);

	sharea->used_cnt = 0;
	sharea->total_cnt = cnt;

//	refresh_num_free(cnt);

	area_was_used = 1;

//	set_gr_pages (SHCOMP_PAGE_CNT);

	printk("shcomp_init: shared compression area initialized\n");

	return sharea;
}

void rm_shcomp_area(struct shcomp_area_info *sharea)
{
	struct shcomp_area *area;

	// move all pages in the used list to the free list
	while(!list_empty(&sharea->used) && sharea->used_cnt > 0) {
		area = list_entry(sharea->used.next, struct shcomp_area, list);
		list_del_entry(&area->list);
		list_add_tail(&area->list, &sharea->free_area);
		sharea->used_cnt--;
	}

/*	while(!list_empty(&sharea->free_area)) {
		area = list_entry(sharea->free_area.next, struct shcomp_area, list);
		list_del_entry(&area->list);

		if(area->page)
			__free_page(area->page);

		vfree(area);
	}
*/
	kfree(sharea);

	area_was_used = 0;
}

static unsigned int init_shcomp_free_page(struct shcomp_area_info *sharea)
{
	unsigned int idx = 0;
	struct shcomp_area *area;

	for(idx=0; idx<SHCOMP_PAGE_CNT; idx++) {
		area = (struct shcomp_area *)vmalloc(sizeof(struct shcomp_area));
		area->addr = get_gntpg_page(idx);
		area->page = get_page_from_vaddr(area->addr);
		area->len = PAGE_SIZE;
		
		list_add_tail(&area->list, &sharea->free_area);
	}

	return idx;
}

struct page *__alloc_shcomp_page(struct shcomp_area_info *sharea)
{
	struct shcomp_area *area;

	if(sharea->used_cnt >= sharea->total_cnt) {
		printk("shcomp_alloc: area is full\n");
		return NULL;
	}

again:
	if(!list_empty(&sharea->free_area)) {
		area = list_entry(sharea->free_area.next, struct shcomp_area, list);
		list_del_entry(&area->list);

		if(find_shcomp_area(&sharea->used, area->addr, NULL)) {
			printk("shcomp_alloc: page %p: already used area\n", area->page);
			goto again;
		}

		sharea->used_cnt++;

		list_add_tail(&area->list, &sharea->used);
	}
	else {
		printk("shcomp_alloc: free list is empty\n");
		return NULL;
	}

	return area->page;
}



void __free_shcomp_page(struct shcomp_area_info *sharea, struct page *page)
{
	struct shcomp_area *area;

	if(sharea->used_cnt <= 0) {
		printk("shcomp_free: it does not exist used areas\n");
		return ;
	}

	area = find_shcomp_area(&sharea->used, NULL, page);
	if(!area) {
		printk("shcomp_free: page %p: No area\n", area->page);
		return ;	
	}

	list_del_entry(&area->list);

	sharea->used_cnt--;

	list_add_tail(&area->list, &sharea->free_area);
}

static struct page *get_page_from_vaddr(void *va)
{
//	if(is_vmalloc_addr(va))
//		return vmalloc_to_page(va);
//	else
		return virt_to_page(va);
}

struct shcomp_area *find_shcomp_area(struct list_head *head, void *addr, struct page *page)
{
	struct shcomp_area *area = NULL;
	struct list_head *pos;

	if(list_empty(head)){
		printk("shcomp_find: list is empty\n");
		return NULL;
	}
	
	if(addr != NULL) {
		list_for_each(pos, head) {
			area = list_entry(pos, struct shcomp_area, list);
			if(area->addr == addr)
				return area;
		}
	}
	else if(page != NULL) {
		list_for_each(pos, head) {
			area = list_entry(pos, struct shcomp_area, list);
			if(area->page == page)
				return area;
		}
	}

	return NULL;
}

//========= functions without parameters =========

void initialize_shcomp(void)
{
	sharea_info = init_shcomp_area();
//	sharea_info = NULL;
//	area_was_used = 0;
}

void remove_shcomp(void)
{
	rm_shcomp_area(sharea_info);
//	sharea_info = NULL;
}

struct page *alloc_shcomp_page(void) 
{
/*	if (sharea_info == NULL || area_was_used == 0) {
		sharea_info = init_shcomp_area();
		printk("shalloc initalized\n");
	}
*/	
	return __alloc_shcomp_page(sharea_info);
}

void free_shcomp_page(struct page *page)
{
	__free_shcomp_page(sharea_info, page);
}

//========= test functions ================

void shcomp_alloc_loop_test(unsigned int count)
{
	int *entry;
	struct page **page;
	int i;
	int idx1=0, idx2=1, idx3=20;

	page = kzalloc(count * sizeof(struct page *), GFP_KERNEL);

	for(i=0; i<count; i++) {
		page[i] = alloc_shcomp_page();
		if(!page[i]) {
			printk("shcomp_control: %d-th allocation failed\n", i);
			return ;
		}
	}

	for(i=0; i<count; i++) {
		entry = (int*)kmap_atomic(page[i], KM_USER0);

		printk("shcomp_control: page[%d]'s address = %p\n", i, entry);

		printk("shcomp_control: inital data: entry[%d] = %d\n", idx1, entry[idx1]);
		printk("shcomp_control: inital data: entry[%d] = %d\n", idx2, entry[idx2]);
		printk("shcomp_control: inital data: entry[%d] = %d\n", idx3, entry[idx3]);

		entry[idx1] = idx1 + 100 + i;
		entry[idx2] = idx2 + 100 + i;
		entry[idx3] = idx3 + 100 + i;
		printk("shcomp_control: after writing: entry[%d] = %d\n", idx1, entry[idx1]);
		printk("shcomp_control: after writing: entry[%d] = %d\n", idx2, entry[idx2]);
		printk("shcomp_control: after writing: entry[%d] = %d\n", idx3, entry[idx3]);

		kunmap_atomic(entry, KM_USER0);
	}

	for(i=0; i<count; i++) {
		shcomp_free_test(page[i]);
		printk("shcomp_control: page[%d] was freed\n", i);
	}

	kfree(page);

}

void shcomp_free_test(struct page *page)
{
	if(!page) {
		printk("shcomp_control: page is null\n");
		return ;
	}
	
	free_shcomp_page(page);

}

