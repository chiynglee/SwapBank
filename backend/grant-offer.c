#include <linux/highmem.h>

#include "common.h"


//int *rtab[RTAB_PAGE_CNT];			// a set of pages for route table data
//int *rtab_refs[RTAB_REF_PAGE_CNT];			// a set of pages with grant references of rtab[][]
//int ref_of_rtab_refs[RTAB_REF_PAGE_CNT];			// grant references of rtab_refs[][]

int **rtab;			// a set of pages for route table data (size = RTAB_PAGE_CNT)
int **rtab_refs;		// a set of pages with grant references of rtab[][] (size = RTAB_REF_PAGE_CNT)
int *ref_of_rtab_refs;		// grant references of rtab_refs[][] (size = RTAB_REF_PAGE_CNT)

static char *change_int_to_str(int *buf, int cnt, int *str_len);
static void init_xensotre_dir(const char *dir, const char *node);	// create a directory in the xenstore
// wirte a grant reference number in the xenstore to share it with other domain
static void write_in_xs(void *refs, const char *dir, const char *node);


/*
 * convert an integer number to a string
 */
static char *change_int_to_str(int *buf, int cnt, int *str_len) {
	int i, len;
	char *outstr, *ptr;

	outstr = (char *)kzalloc(*str_len, GFP_KERNEL);
	ptr = outstr;
	for(i=0; i<cnt; i++) {
		len = sprintf(ptr, "%d/", buf[i]);
		ptr = ptr + len;
	}

	*str_len = ptr - outstr;

	return outstr;
}

/*
 * create a directory in xen store
*/
static void init_xensotre_dir(const char *dir, const char *node) {
	struct xenbus_transaction xbt;
	int err;

	err = xenbus_transaction_start(&xbt);
	if(err) {
		printk("init_xenstore_dir() : transaction start fail\n");
		return ;
	}

	err = xenbus_mkdir(xbt, dir, node);
	if(err) {
		printk("init_xenstore_dir() : mkdir fail\n");
		xenbus_transaction_end(xbt, 1);
		return ;
	}
	
	xenbus_transaction_end(xbt, 0);
}

/*
 * wirte data in xen store
*/
static void write_in_xs(void *refs, const char *dir, const char *node) {
	struct xenbus_transaction xbt;
	int err;

	err = xenbus_transaction_start(&xbt);
	if (err) {
		printk("write_in_xs() : transaction start fail\n");
		return;
	}

	err = xenbus_write(xbt, dir, node, (char *)refs);
	if(err) {
		printk("write_in_xs() : writing a physical address was fail\n");
		xenbus_transaction_end(xbt, 1);
	}

	xenbus_transaction_end(xbt, 0);
}

/*
 * create a granted page
*/

int prev_ref = 0;

int set_grant_page(unsigned int remote_domid, void *va) 
{
	unsigned long frame;
	int ref=0;

	frame = virt_to_mfn(va);
	
	ref = gnttab_grant_foreign_access(remote_domid, frame, RW);
	if(ref < 0) {
		printk("set_grant_page() : gnttab_grant_foreign_access() was failed (previous ref = %d)\n", prev_ref);
		return ref;
	}

	prev_ref = ref;
	
	return ref;
}

/*
 * create level 1 pages
 * ( i.e. create page with grant reference numbers of other pages )
 */
int create_lvl1_page(int *refs, void **va, unsigned int page_cnt, 
					const char *dir, const char *node, unsigned int domid) 
{
	int pgidx;
	char *str;
	int len = 128;

	for(pgidx=0; pgidx<page_cnt; pgidx++) {
		va[pgidx] = (void*)get_zeroed_page(GFP_NOIO | __GFP_HIGHMEM);
//		va[pgidx] = (void*)get_zeroed_page(GFP_KERNEL);
		if(!va[pgidx]) {
			printk("create_refs_page() : get_zeroed_page() for rtab_refs[%d] was failed\n", pgidx);
			return -1;
		}
		refs[pgidx] = set_grant_page(domid, va[pgidx]);
	}
	
	str = change_int_to_str(refs, page_cnt, &len);
	write_in_xs(str, dir, node);

	kfree(str);

	return 0;
}

/*
 * create level 2 pages and 
 * save grant reference numbers of the pages into other granted pages
 * ( i.e. create grant pages for a routing table, and grant pages for packet informations )
 */
int create_lvl2_page(int **refs, void **pages, 
					unsigned int page_cnt, unsigned int ref_page_cnt, unsigned int domid)
{
	int pg_idx, ref_idx, i;
	int content_page_idx = 0;

	for(pg_idx=0; pg_idx < page_cnt; pg_idx++) {
//		pages[pg_idx] = (void *)get_zeroed_page(GFP_NOIO | __GFP_HIGHMEM);
		pages[pg_idx] = (void *)get_zeroed_page(GFP_KERNEL);
		if(!pages[pg_idx]) {
			printk("create_pktinfo_page() : get_zeroed_page() was failed\n");
			for(i=0; i<pg_idx; i++)
				free_page((unsigned long)pages[pg_idx]);
			return -1;
		}
	}

	for(pg_idx=0; pg_idx < ref_page_cnt; pg_idx++) {
		for(ref_idx=0; ref_idx < REF_CNT_IN_1PAGE; ref_idx++) {
			content_page_idx = (pg_idx * REF_CNT_IN_1PAGE) + ref_idx;

			if(content_page_idx >= page_cnt) {
				return ref_idx;
			}

			refs[pg_idx][ref_idx] = set_grant_page(domid, pages[content_page_idx]);
		}
	}

	return 0;
}

/*
 * deallocate the granted pages for grant reference numbers
 */
void remove_lvl1_page(int *refs, void **pages, unsigned int page_cnt) {
	int pgidx;

	for(pgidx=0; pgidx<page_cnt; pgidx++) {
		if(refs[pgidx] != 0)
			gnttab_end_foreign_access(refs[pgidx], RW, (unsigned long)pages[pgidx]);
	}
}

/*
 * deallocate the granted pages
*/
void remove_lvl2_page(int **refs, void **pages, unsigned int ref_page_cnt)
{
	int pg_cnt, ref_cnt;
	int content_page_idx = 0;

	for(pg_cnt=0; pg_cnt < ref_page_cnt; pg_cnt++) {
		if(refs[pg_cnt] == NULL)
			continue;
		
		for(ref_cnt=0; ref_cnt < REF_CNT_IN_1PAGE; ref_cnt++) {
			content_page_idx = (pg_cnt * REF_CNT_IN_1PAGE) + ref_cnt;

			if(refs[pg_cnt][ref_cnt] != 0) {
				gnttab_end_foreign_access(refs[pg_cnt][ref_cnt], RW, (unsigned long)pages[content_page_idx]);
			}
		}
	}
}

int offering_space_bw_doms(struct xenbus_device *xbdev) {
	int err;
	int lastidx = 0;
	unsigned int remote_domid = xbdev->otherend_id;

	init_xensotre_dir(xbdev->otherend, REF_NODE_FOR_RTAB);

	rtab = (int**)vmalloc(sizeof(void*)*RTAB_PAGE_CNT);
	rtab_refs = (int**)vmalloc(sizeof(void*)*RTAB_REF_PAGE_CNT);
	ref_of_rtab_refs = (int*)vmalloc(sizeof(void*)*RTAB_REF_PAGE_CNT);

	err = create_lvl1_page(ref_of_rtab_refs, (void**)rtab_refs, RTAB_REF_PAGE_CNT, 
						xbdev->otherend, REF_NODE_FOR_RTAB, remote_domid);

#ifdef GNT_DEBUG
	printk("grant_offer.c : the number of free pages = %ld\n", atomic_long_read(&vm_stat[NR_FREE_PAGES]));
	printk("grant_offer.c : grant ref = %d, value(rtab[0][0]) = %d, err = %d\n", ref_of_rtab_refs[0], rtab_refs[0][0], err);
#endif

	err = create_lvl2_page(rtab_refs, (void**)rtab, RTAB_PAGE_CNT, RTAB_REF_PAGE_CNT, remote_domid);
	if(err >= 0) {
		for(lastidx=0; lastidx<RTAB_PAGE_CNT; lastidx++) {
			rtab[lastidx][0] = lastidx;
			rtab[lastidx][1] = lastidx+10000;
			rtab[lastidx][20] = lastidx+20000;
		}

#ifdef GNT_DEBUG
		printk("grant_offer.c : the number of free pages = %ld\n", atomic_long_read(&vm_stat[NR_FREE_PAGES]));
		printk("grant_offer.c : page[0]'s ref = %d, 0-th value = %d \n", rtab_refs[0][0], rtab[0][0]);
		printk("grant_offer.c : page[%d]'s ref = %d, 0-th value = %d \n", lastidx-1, 
									rtab_refs[RTAB_REF_PAGE_CNT-1][err-1], rtab[lastidx-1][0]);
#endif
	}
	
	return 0;
}
 
void removing_space_bw_doms(void) {
	remove_lvl2_page(rtab_refs, (void**)rtab, RTAB_REF_PAGE_CNT);
	remove_lvl1_page(ref_of_rtab_refs, (void**)rtab_refs, RTAB_REF_PAGE_CNT);

	vfree(rtab);
	vfree(rtab_refs);
	vfree(ref_of_rtab_refs);
}

