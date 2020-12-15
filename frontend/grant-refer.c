#include "common.h"


// level 2 pages
struct gntpg_t *pgs = NULL;		// a set of pages for route table data

// level 1 pages
//struct gntpg_t refs[RTAB_REF_PAGE_CNT];		// a set of pages with grant references of rtab[][]
struct gntpg_t *refs = NULL;		// a set of pages with grant references of rtab[][]

static int str_to_int(char *str, int degree, char sym, int *out);
static int parsing_str(struct gntpg_t *gnt, char *str);
static char *read_from_xs(const char *dir, const char *node, int *len);


int gntpage_is_ready(void)
{
	if(pgs == NULL)
		return 0;
	else
		return 1;
}

void *get_gntpg_page(unsigned int index)
{
	if(!pgs)
		return NULL;
	else
		return pgs[index].va;
}

/*
 * convert a string to a number
*/
static int str_to_int(char *str, int degree, char sym, int *out){
	int i=0, len=0, num=0;

	while(str[i] != sym) {
		num *= degree;
		if(str[i] >= '0' && str[i] <= '9')
			num += str[i++] - 48;
		else if(str[i] >= 'a' && str[i] <= 'f')
			num += str[i++] - 87;
	}

	*out = num;
	len = i + 1;

	return len;
}

/*
 * extract numbers from a string
*/
static int parsing_str(struct gntpg_t *gnt, char *str) {
	int idx=0, i=0, len=0;

	if(str == NULL || gnt == NULL) {
		printk("parsing_str() : do not allocate space (str = %s)\n", str);
		return -1;
	}

	for(i=0; str[idx] != '\0'; i++) {
		len = str_to_int(&(str[idx]), 10, '/', &(gnt[i].ref));
		idx += len;
	}

	return i;
}

/*
 * read data from xen store.
*/
static char *read_from_xs(const char *dir, const char *node, int *len) {
	int err;
	char *out;
	
	out = (char *)xenbus_read(XBT_NIL, dir, node, len);
	if((err=IS_ERR(out))) {
		printk("read_from_xs() : reading a physical address was fail : %d\n", err);
		return NULL;
	}

	return out;
}

/*
 * get grant reference numbers from xen store.
*/
int get_gntref_number(struct gntpg_t *gnt, const char* dir, const char *node) {
	char *str_ref=NULL;
	int len=0;
	int i;

	str_ref = read_from_xs(dir, node, &len);
	if(len <= 0) {
		return -1;
	}
#ifdef GNT_DEBUG
	printk("get_gntref_number() : reading = %s\n", str_ref);
#endif

	len = parsing_str(gnt, str_ref);
	if(len <= 0) {
		printk("get_gntref_number() : ref values does not exist.\n");
		return -2;
	}

#ifdef GNT_DEBUG	
	for(i=0; i<len; i++)
		printk("get_gntref_number() : ref = %d\n", gnt[i].ref);
#endif

	// because xenbus_read() returns a page's pointer allocated by page_alloc()
	kfree(str_ref);

	return len;
}

/*
 * map grants for pages.
 * This function takes the virtual addresses for granted pages using grant referece numbers.
 * The virtual address is created using xen_alloc_vm_area().
*/
int map_gnt_page(struct gntpg_t *gnt, unsigned int count, unsigned int domid) {
	struct gnttab_map_grant_ref *map_op;
	int i;
	
//	map_op = (struct gnttab_map_grant_ref *)kzalloc(sizeof(struct gnttab_map_grant_ref)*count, GFP_KERNEL);
	map_op = (struct gnttab_map_grant_ref *)vmalloc(sizeof(struct gnttab_map_grant_ref)*count);
	if(map_op == NULL) {
		printk("map_gnt_page() : map_op is NULL\n");
		return -1;
	}

	for(i=0; i<count; i++) {
		gnt[i].area = xen_alloc_vm_area(PAGE_SIZE);
		if (!gnt[i].area) {
			printk("map_gnt_page() : vm area is NULL\n");
			return -2;
		}
		
		gnt[i].va = gnt[i].area->addr;
		
		set_map_structure(&(map_op[i]), (unsigned long)(gnt[i].va), GNTMAP_host_map, gnt[i].ref, (domid_t)domid);
	}

	HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map_op, count);

	for(i=0; i<count; i++) {
		gnt[i].handle = map_op[i].handle;
		if(map_op[i].status != GNTST_okay) {
			printk("map_gnt_page() : map_op[%d]'s hypercall failed (%d)\n", i, map_op[i].status);
			goto err_out;
		}
	}

//	kfree(map_op);
	vfree(map_op);
	return 0;

err_out:

	for(i=0; i<count; i++) {
		if(map_op[i].status == GNTST_okay)
			unmap_gnt_page(&gnt[i], 1);
		else
			xen_free_vm_area(gnt[i].area);
	}

//	kfree(map_op);
	vfree(map_op);
	return -3;
}

/*
 * unmap grants for pages
 * It just unmap grants, does not deallocates the pages.
 * But the vm area created to get a virtual address is deallocated.
*/
void unmap_gnt_page(struct gntpg_t *gnt, unsigned int count) {
	struct gnttab_unmap_grant_ref *unmap_op;
	int i;
	int ret;

//	unmap_op = (struct gnttab_unmap_grant_ref *)kzalloc(sizeof(struct gnttab_unmap_grant_ref)*count, GFP_KERNEL);
	unmap_op = (struct gnttab_unmap_grant_ref *)vmalloc(sizeof(struct gnttab_unmap_grant_ref)*count);
	if(unmap_op == NULL) {
		printk("unmap_gnt_page() : unmap_op is NULL\n");
		return ;
	}

	for(i=0; i<count; i++) {
		if(gnt[i].va != NULL) {
			set_unmap_structure(&(unmap_op[i]), (unsigned long)(gnt[i].va), GNTMAP_host_map, gnt[i].handle);
			gnt[i].handle = ~0;
		}
	}

unmap_hypercall:
	ret = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, unmap_op, count);

	printk("[unmap hypercall] retrun value = %d\n", ret);
	if(ret != 0) {
		msleep(1000);
		goto unmap_hypercall;
	}

	for(i=0; i<count; i++)
		xen_free_vm_area(gnt[i].area);

//	kfree(unmap_op);
	vfree(unmap_op);
}

void *map_lvl2_pages(unsigned int count, struct gntpg_t *input, unsigned int domid, 
						unsigned int refpg_cnt) {
	int err=0, pg, ref_in_pg, out=0;
	int *refnum;
	struct gntpg_t *gnt;

	gnt = (struct gntpg_t *)vmalloc(sizeof(struct gntpg_t)*count);
	if(!gnt) {
		printk("map_rtab_pages() : gnt is NULL.\n");
		return NULL;
	}

	for(pg=0; pg<refpg_cnt; pg++) {
		refnum = (int*)(input[pg].va);

		for(ref_in_pg=0; ref_in_pg<REF_CNT_IN_1PAGE; ref_in_pg++) {
			if(out >= count) 
				goto escape_loop;

			gnt[out++].ref = refnum[ref_in_pg];
		}
	}

escape_loop:

	err = map_gnt_page(gnt, count, domid);
	if(err < 0) {
		printk("map_rtab_pages() : map_gnt_page() for rtab was failed (%d).\n", err);
		vfree(gnt);
		return NULL;
	}

	return gnt;
}

void unmap_lvl2_pages (struct gntpg_t *gnt, unsigned int count) {
	if(!gnt) {
		printk("unmap_vfree_pages() : gnt is NULL.\n");
		return;
	}

	unmap_gnt_page(gnt, count);
	vfree(gnt);
}

int mapping_space_bw_doms(struct xenbus_device *xbdev) {
	unsigned int remote_domid = xbdev->otherend_id;
	int len_ref;
	int err=0;
	unsigned int size = RTAB_PAGE_CNT;

	printk("domain name = %s, domain id = %d\n", xbdev->nodename, remote_domid);
	
	// create "refs"
	refs = (struct gntpg_t*)vmalloc(sizeof(struct gntpg_t)*RTAB_REF_PAGE_CNT);

	len_ref = get_gntref_number(refs, xbdev->nodename, REF_NODE_FOR_RTAB);
	if(len_ref < 0) {
		printk("grant_refer.c : get_gntref_number() failed. (len = %d)\n", len_ref);
		goto err_out;
	}

	err = map_gnt_page(refs, RTAB_REF_PAGE_CNT, remote_domid);
	if(err < 0) {
		printk("grant_refer.c : map_gnt_page() was failed (%d).\n", err);
		goto err_out;
	}

#ifdef GNT_DEBUG
	printk("grant_refer.c : the number of free pages = %ld\n", atomic_long_read(&vm_stat[NR_FREE_PAGES]));
	printk("grant_refer.c : refs's value = %d\n", ((int*)(refs[0].va))[0]);
	printk("grant_refer.c : refs's value = %d\n", ((int*)(refs[0].va))[1]);
#endif

	pgs = (struct gntpg_t *)map_lvl2_pages(size, refs, remote_domid, RTAB_REF_PAGE_CNT);
	if(!pgs) {
		printk("grant_refer.c : map_rtab_pages() was failed (%d).\n", err);
		goto err_out;
	}

#ifdef GNT_DEBUG
	printk("grant_refer.c : the number of free pages = %ld\n", atomic_long_read(&vm_stat[NR_FREE_PAGES]));
	printk("grant_refer.c : page[%d]'s 0-th integer value = %d\n", 1, ((int*)(pgs[1].va))[0]);
	printk("grant_refer.c : page[%d]'s 0-th integer value = %d\n", size-1, ((int*)(pgs[size-1].va))[0]);
#endif

	return 0;

err_out:
	unmapping_space_bw_doms();

	return -1;
}

void unmapping_space_bw_doms(void) {
	if(pgs != NULL)
		unmap_lvl2_pages(pgs, RTAB_PAGE_CNT);

	if(refs->area != NULL)
		unmap_gnt_page(refs, RTAB_REF_PAGE_CNT);

	if(refs != NULL)
		vfree(refs);
}

