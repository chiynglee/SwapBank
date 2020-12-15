#ifndef _SHCOMP_REFER_COMMON_H_
#define _SHCOMP_REFER_COMMON_H_

#include "frontend_xenbus.h"

#define GNT_DEBUG

#define RONLY 1
#define RW 0

#define REF_NODE_FOR_RTAB "rtab_refs"

struct gntpg_t {
	domid_t domid;
	grant_handle_t handle;
	grant_ref_t ref;
	void *va;
	struct vm_struct *area;
};

#define MBytes (1024*1024)

#define REF_CNT_IN_1PAGE (PAGE_SIZE / sizeof(int))
//#define RTAB_SIZE (50*MBytes)
#define RTAB_SIZE (get_size_of_swapspace()*1024)
#define RTAB_PAGE_CNT DIV_ROUND_UP(RTAB_SIZE, PAGE_SIZE)
#define RTAB_REF_PAGE_CNT DIV_ROUND_UP(RTAB_PAGE_CNT, REF_CNT_IN_1PAGE)


// get a grant reference number for a granted page using the xenstore
int get_gntref_number(struct gntpg_t *gnt, const char *dir, const char *node);

// map granted pages to virtual addresses
int map_gnt_page(struct gntpg_t *gnt, unsigned int count, unsigned int domid);
// unmap granted pages from virtual addresses
void unmap_gnt_page(struct gntpg_t *gnt, unsigned int count);

// map granted pages of level 2 using another grant page with a referecce number
void *map_lvl2_pages(unsigned int count, struct gntpg_t *input, unsigned int domid, 
						unsigned int refpg_cnt);
// unmap granted pages of level 2
void unmap_lvl2_pages (struct gntpg_t *gnt, unsigned int count);

int mapping_space_bw_doms(struct xenbus_device *xbdev);
void unmapping_space_bw_doms(void);

static inline void set_map_structure(struct gnttab_map_grant_ref *map, 
					phys_addr_t addr, uint32_t flags, grant_ref_t ref, domid_t domid)
{
	if (flags & GNTMAP_contains_pte)
		map->host_addr = addr;
	else if (xen_feature(XENFEAT_auto_translated_physmap))
		map->host_addr = __pa(addr);
	else
		map->host_addr = addr;

	map->flags = flags;
	map->ref = ref;
	map->dom = domid;
}
static inline void set_unmap_structure(struct gnttab_unmap_grant_ref *unmap, 
							phys_addr_t addr, uint32_t flags, grant_handle_t handle)
{
	if (flags & GNTMAP_contains_pte)
		unmap->host_addr = addr;
	else if (xen_feature(XENFEAT_auto_translated_physmap))
		unmap->host_addr = __pa(addr);
	else
		unmap->host_addr = addr;

	unmap->handle = handle;
	unmap->dev_bus_addr = 0;
}

void *get_gntpg_page(unsigned int index);
int gntpage_is_ready(void);

#endif
