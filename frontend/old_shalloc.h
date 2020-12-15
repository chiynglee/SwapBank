#include <linux/list.h>
#include <linux/highmem.h>


#define SHCOMP_PAGE_CNT RTAB_PAGE_CNT

struct shcomp_area {
	struct list_head list;
	unsigned int len;
	void *addr;
	struct page *page;
};

struct shcomp_area_info {
	unsigned int total_cnt;
	unsigned int used_cnt;
	struct list_head free_area;
	struct list_head used;
};

void *init_shcomp_area(void);
void rm_shcomp_area(struct shcomp_area_info *sharea);

//void *alloc_shcomp_page(struct shcomp_area_info *sharea);
//void free_shcomp_page(struct shcomp_area_info *sharea, void *area);
struct page *__alloc_shcomp_page(struct shcomp_area_info *sharea);
void __free_shcomp_page(struct shcomp_area_info *sharea, struct page *page);

int shcomp_is_ready(void);
struct shcomp_area *find_shcomp_area(struct list_head *head, void *addr, struct page *page);


//========= functions without parameters =========
void initialize_shcomp(void);
void remove_shcomp(void);
struct page *alloc_shcomp_page(void);
void free_shcomp_page(struct page *page);


// =========== test functions =================
void shcomp_alloc_loop_test(unsigned int count);
void shcomp_free_test(struct page *page);

