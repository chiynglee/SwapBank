#ifndef _SHCOMP_BACKEND_XENBUS_H_
#define _SHCOMP_BACKEND_XENBUS_H_

#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
//#include <linux/jiffies.h>

#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/xen.h>
#include <xen/events.h>
#include <xen/interface/io/ring.h>

#include <asm/xen/page.h>

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

/*	union {
		uint16_t outif;
		uint16_t status;
	};
*/
};

DEFINE_RING_TYPES(rshm, struct rshm_request_t, struct rshm_response_t);

struct rshmback_t {
	// Unique identifier for this interface
	domid_t           domid;
	unsigned int      handle;

	atomic_t         refcnt;

	// interrupt
	unsigned int     irq;
	
	// Comms information
	struct rshm_back_ring ring;
	struct vm_struct *ring_area;
	spinlock_t       ring_lock;


	// Back pointer to the backend_info
	struct backend_info *be;

	wait_queue_head_t   wq;
	struct task_struct  *rshm_evt_d;
	unsigned int        waiting_reqs;
	wait_queue_head_t waiting_to_free;

	// fields for a grant mechanism
	grant_handle_t shmem_handle;
	grant_ref_t    shmem_ref;
};

struct backend_info {
	struct xenbus_device *dev;
	struct rshmback_t *rshm;
};

unsigned long get_size_of_swapspace(void);

int rshm_be_schedule(void *arg);
irqreturn_t rshm_be_interrupt(int irq, void *dev_id);

static inline void rshm_get(struct rshmback_t *rshm)
{
	atomic_inc(&rshm->refcnt);
}

static inline void  rshm_put(struct rshmback_t *rshm)
{
	if (atomic_dec_and_test(&rshm->refcnt))
		wake_up(&rshm->waiting_to_free);
}

static inline void set_resp_ok(struct rshm_response_t *resp, const struct rshm_request_t *req, uint8_t op)
{
	resp->id = req->id;
	resp->ref = req->ref;
	resp->offset = req->offset;
	resp->operation = op;
	resp->status = RSP_OKAY;
}

static inline void set_resp_error(struct rshm_response_t *resp, const struct rshm_request_t *req, uint8_t op)
{
	resp->id = req->id;
	resp->ref = req->ref;
	resp->offset = req->offset;
	resp->operation = op;
	resp->status = RSP_ERROR;
}

#endif
