#ifndef _SHCOMP_FRONTEND_XENBUS_H_
#define _SHCOMP_FRONTEND_XENBUS_H_ 

#include <linux/version.h>

#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/interface/io/ring.h>
#include <xen/events.h>

#include <asm/xen/page.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32))
#include <xen/xen.h>
#else
#include <asm/xen/hypervisor.h>
#include <xen/interface/xen.h>
#endif


 // Operation failed for some unspecified reason (-EIO).
#define RSP_ERROR       -1
 // Operation completed successfully.
#define RSP_OKAY         0

#define REQ_MAP 1
#define REQ_UNMAP 2
#define REQ_FIND 3

#define RESP_MAP 1
#define RESP_UNMAP 2
#define RESP_FIND 3

#define INVALID_REF	0
#define INVALID_OFFSET	0

struct rshm_request_t {
	uint8_t operation;
	uint64_t id;
	uint32_t ref;
	uint16_t offset;
	uint8_t map_lvl;
};

struct rshm_response_t {
	uint8_t operation;
	uint64_t id;
	uint32_t ref;
	uint16_t offset;
	uint16_t status;
};

DEFINE_RING_TYPES(rshm, struct rshm_request_t, struct rshm_response_t);

#define RSHM_RING_SIZE __RING_SIZE((struct rshm_sring *)0, PAGE_SIZE)

struct rshmfront_info_t {
	struct xenbus_device *xbdev;
	unsigned int handle;
	
	spinlock_t ring_lock;
	int ring_ref;
	struct rshm_front_ring ring;
	unsigned int evtchn;
	unsigned int irq;

	wait_queue_head_t   wq;
	struct task_struct  *rshm_evt_d;
	unsigned int waiting_msgs;
	
	struct rshm_request_t sended_reqs[RSHM_RING_SIZE];
	unsigned long free_id;
};

unsigned long get_size_of_swapspace(void);

void send_requests(struct rshmfront_info_t *info);
void create_rquest(struct rshmfront_info_t *info, 
			uint8_t op, uint32_t ref, uint16_t offset, uint8_t lvl);

irqreturn_t rshm_fe_interrupt(int irq, void *dev_id);
int rshm_fe_schedule(void *arg);


#endif
