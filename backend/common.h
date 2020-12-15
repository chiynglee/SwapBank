#ifndef _SHCOMP_OFFER_COMMON_H_
#define _SHCOMP_OFFER_COMMON_H_

#include "backend_xenbus.h"

#define GNT_DEBUG

#define RONLY 1
#define RW 0

#define REF_NODE_FOR_RTAB "rtab_refs"

#define KBytes 1024
#define MBytes (1024*KBytes)

#define REF_CNT_IN_1PAGE (PAGE_SIZE / sizeof(int))
//#define RTAB_SIZE (50*MBytes)
#define RTAB_SIZE (get_size_of_swapspace() * 1024)
#define RTAB_PAGE_CNT DIV_ROUND_UP(RTAB_SIZE, PAGE_SIZE)
#define RTAB_REF_PAGE_CNT DIV_ROUND_UP(RTAB_PAGE_CNT, REF_CNT_IN_1PAGE)


int set_grant_page(unsigned int remote_domid, void *va);
int create_lvl1_page(int *refs, void **pages, unsigned int page_cnt, 
					const char *dir, const char *node, unsigned int domid);
int create_lvl2_page(int **refs, void **pages, 
					unsigned int page_cnt, unsigned int ref_page_cntm, unsigned int domid);
void remove_lvl1_page(int *refs, void **pages, unsigned int page_cnt);
void remove_lvl2_page(int **refs, void **pages, unsigned int ref_page_cnt);

int offering_space_bw_doms(struct xenbus_device *xbdev);
void removing_space_bw_doms(void);

#endif
