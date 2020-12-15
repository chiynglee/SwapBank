#include <linux/module.h>

#include "common.h"

static int do_rshm_op(struct rshmback_t *rshm);
static void send_response(struct rshmback_t *rshm, struct rshm_response_t *resp);

static int map_ring_page(struct rshmback_t *rshm, unsigned long shared_page);
static void unmap_ring_page(struct rshmback_t *rshm);

static int connect_ring(struct backend_info *be);
static void rshm_disconnect(struct rshmback_t *rshm);

static struct rshmback_t *rshm_alloc(domid_t domid);
static void rshmback_free(struct rshmback_t *rshm);
static int rshm_map(struct rshmback_t *rshm, unsigned long shared_page, unsigned int evtchn);

static int rshmback_probe(struct xenbus_device *xbdev,
			 const struct xenbus_device_id *id);
static int rshmback_remove(struct xenbus_device *xbdev);
static void frontend_changed(struct xenbus_device *xbdev,
			     enum xenbus_state frontend_state);
static void update_rshm_status(struct rshmback_t *rshm);


static unsigned long size_kb=0;

unsigned long get_size_of_swapspace(void)
{
	// default size = 50MB
	if(size_kb == 0)
		return 50*1024;
	else
		return size_kb;
}

/*
 * Callback received when the frontend's state changes.
 */
static void frontend_changed(struct xenbus_device *xbdev,
			     enum xenbus_state frontend_state)
{
	struct backend_info *be = dev_get_drvdata(&xbdev->dev);
	int err;

#ifdef GNT_DEBUG
	printk("\n frontend_changed() start : be-state = %d fe-state = %d\n", xbdev->state, frontend_state);
#endif

	switch (frontend_state) {
	case XenbusStateInitialising:
		if (xbdev->state == XenbusStateClosed) {
			printk(KERN_INFO "%s: %s: prepare for reconnect\n",
			       __FUNCTION__, xbdev->nodename);
			xenbus_switch_state(xbdev, XenbusStateInitWait);
		}
		break;

	case XenbusStateInitialised:
	case XenbusStateConnected:
		if (xbdev->state == XenbusStateConnected)
			break;

		err = connect_ring(be);
		if (err)
			break;
		update_rshm_status(be->rshm);
		break;

	case XenbusStateClosing:
//		unmapping_space_bw_doms();
//		rshm_disconnect(be->rshm);
		xenbus_switch_state(xbdev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		xenbus_switch_state(xbdev, XenbusStateClosed);
		rshmback_remove(xbdev);
		if (xenbus_dev_is_online(xbdev))
			break;
	case XenbusStateUnknown:
		device_unregister(&xbdev->dev);
		break;

	default:
		xenbus_dev_fatal(xbdev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}

static void update_rshm_status(struct rshmback_t *rshm)
{
	int err;
	char name[TASK_COMM_LEN];

	// Not ready to connect?
	if (!rshm->irq)
		return;

	// Already connected?
	if (rshm->be->dev->state == XenbusStateConnected)
		return;

	xenbus_switch_state(rshm->be->dev, XenbusStateConnected);

	snprintf(name, TASK_COMM_LEN, "rshmback.%d", rshm->domid);

	rshm->rshm_evt_d = kthread_run(rshm_be_schedule, rshm, name);
	if (IS_ERR(rshm->rshm_evt_d)) {
		err = PTR_ERR(rshm->rshm_evt_d);
		rshm->rshm_evt_d = NULL;
		xenbus_dev_error(rshm->be->dev, err, "start rshm_evt_d");
	}
}

int rshm_be_schedule(void *arg)
{
	struct rshmback_t *rshm = arg;

	rshm_get(rshm);

#ifdef GNT_DEBUG
	printk(KERN_DEBUG "%s: started\n", current->comm);
#endif

	while (!kthread_should_stop()) {
		if (try_to_freeze())
			continue;

		wait_event_interruptible(
			rshm->wq,
			rshm->waiting_reqs || kthread_should_stop());

		rshm->waiting_reqs = 0;
		smp_mb(); // clear flag

		if (do_rshm_op(rshm))
			rshm->waiting_reqs = 1;
	}

#ifdef GNT_DEBUG
	printk(KERN_DEBUG "%s: exiting\n", current->comm);
#endif

	rshm->rshm_evt_d = NULL;

	rshm_put(rshm);
	
	return 0;
}

static int do_rshm_op(struct rshmback_t *rshm)
{
	struct rshm_back_ring *ring = &rshm->ring;
	struct rshm_request_t req;
	struct rshm_response_t *resp;
	RING_IDX rc, rp;
	int more_to_do = 0;
	int err = 0;

	rc = ring->req_cons;
	rp = ring->sring->req_prod;
	rmb(); // Ensure we see queued requests up to 'rp'.

	while (rc != rp) {
		if (RING_REQUEST_CONS_OVERFLOW(ring, rc))
			break;

		if (kthread_should_stop()) {
			more_to_do = 1;
			break;
		}

		memcpy(&req, RING_GET_REQUEST(ring, rc), sizeof(req));
		ring->req_cons = ++rc;

		// Apply all sanity checks to /private copy/ of request.
		barrier();

		switch (req.operation) {
		case REQ_MAP:
			if(req.ref == INVALID_REF && req.map_lvl == 2) {
				err = offering_space_bw_doms(rshm->be->dev);
				if(err < 0) {
					printk("create and map space fail\n");
				}

				resp = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
				ring->rsp_prod_pvt++;

				set_resp_ok(resp, &req, RESP_MAP);
				send_response(rshm, resp);
//				err = mapping_space_bw_doms(rshm);
			}

			break;
		case REQ_UNMAP: 
			if(req.ref == INVALID_REF && req.map_lvl == 2) {
				removing_space_bw_doms();
//				unmapping_space_bw_doms();

				resp = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
				ring->rsp_prod_pvt++;

				set_resp_ok(resp, &req, RESP_UNMAP);
				send_response(rshm, resp);
			}
			break;
		case REQ_FIND:
			break;
		default:
			msleep(1);
			printk("error: unknown io operation [%d]\n", req.operation);

			resp = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
			ring->rsp_prod_pvt++;
			
			set_resp_error(resp, &req, req.operation);
			send_response(rshm, resp);
			break;
		}

		// Yield point for this unbounded loop.
		cond_resched();
	}

	return more_to_do;
}

static void send_response(struct rshmback_t *rshm, struct rshm_response_t *resp)
{
	unsigned long     flags;
	struct rshm_back_ring *ring = &rshm->ring;
	int more_to_do = 0;
	int notify;

	spin_lock_irqsave(&rshm->ring_lock, flags);
	
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(ring, notify);
	if (ring->rsp_prod_pvt == ring->req_cons) {
		// Tail check for pending requests. Allows frontend to avoid
		// notifications if requests are already in flight (lower
		// overheads and promotes batching).
		RING_FINAL_CHECK_FOR_REQUESTS(ring, more_to_do);
	} else if (RING_HAS_UNCONSUMED_REQUESTS(ring)) {
		more_to_do = 1;
	}

	spin_unlock_irqrestore(&rshm->ring_lock, flags);

	if (more_to_do) {
		rshm->waiting_reqs = 1;
		wake_up(&rshm->wq);
	}
	if (notify)
		notify_remote_via_irq(rshm->irq);
}

static int connect_ring(struct backend_info *be)
{
	struct xenbus_device *xbdev = be->dev;
	unsigned long ring_ref;
	unsigned int evtchn;
	int err;

	err = xenbus_gather(XBT_NIL, xbdev->otherend, "ring-ref", "%lu", &ring_ref,
			    "event-channel", "%u", &evtchn, NULL);
	if (err) {
		xenbus_dev_fatal(xbdev, err,
				 "reading %s/ring-ref and event-channel",
				 xbdev->otherend);
		return err;
	}

	// Map the shared frame, irq etc. 
	err = rshm_map(be->rshm, ring_ref, evtchn);
	if (err) {
		xenbus_dev_fatal(xbdev, err, "mapping ring-ref %lu port %u",
				 ring_ref, evtchn);
		return err;
	}

#ifdef GNT_DEBUG
	printk("rshm shared ring connected to domain %d (%s)\n", xbdev->otherend_id, xbdev->nodename);
#endif

	return 0;
}

static int rshm_map(struct rshmback_t *rshm, unsigned long shared_page, unsigned int evtchn)
{
	int err;
	struct rshm_sring *sring;

	if (rshm->irq)
		return 0;

	if ( (rshm->ring_area = alloc_vm_area(PAGE_SIZE)) == NULL )
		return -ENOMEM;

	err = map_ring_page(rshm, shared_page);
	if (err) {
		free_vm_area(rshm->ring_area);
		return err;
	}

	sring = (struct rshm_sring *)rshm->ring_area->addr;
	BACK_RING_INIT(&rshm->ring, sring, PAGE_SIZE);

	err = bind_interdomain_evtchn_to_irqhandler(
		rshm->domid, evtchn, rshm_be_interrupt, 0, "rshm-backend", rshm);
	if (err < 0)
	{
		unmap_ring_page(rshm);
		free_vm_area(rshm->ring_area);
		rshm->ring.sring = NULL;
		return err;
	}
	rshm->irq = err;

	return 0;
}

/*
 * NOTIFICATION FROM GUEST OS.
 */
irqreturn_t rshm_be_interrupt(int irq, void *dev_id)
{
	struct rshmback_t *rshm;

	rshm = (struct rshmback_t *) dev_id;
	rshm->waiting_reqs = 1;
	wake_up(&rshm->wq);

	return IRQ_HANDLED;
}

static int map_ring_page(struct rshmback_t *rshm, unsigned long shared_page)
{
	struct gnttab_map_grant_ref op;

	gnttab_set_map_op(&op, (unsigned long)rshm->ring_area->addr,
			  GNTMAP_host_map, shared_page, rshm->domid);

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &op, 1))
		BUG();

	if (op.status) {
		printk("map_ring_page() : Grant table operation failure !\n");
		return op.status;
	}

	rshm->shmem_ref = shared_page;
	rshm->shmem_handle = op.handle;

	return 0;
}

static void unmap_ring_page(struct rshmback_t *rshm)
{
	struct gnttab_unmap_grant_ref op;

	gnttab_set_unmap_op(&op, (unsigned long)rshm->ring_area->addr,
			    GNTMAP_host_map, rshm->shmem_handle);

	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &op, 1))
		BUG();
}

static struct rshmback_t *rshm_alloc(domid_t domid) 
{
	struct rshmback_t *rshm;

	rshm = kzalloc(sizeof(struct rshmback_t), GFP_KERNEL);
	if (!rshm)
		return ERR_PTR(-ENOMEM);

	memset(rshm, 0, sizeof(*rshm));
	rshm->domid = domid;
	spin_lock_init(&rshm->ring_lock);
	atomic_set(&rshm->refcnt, 1);
	init_waitqueue_head(&rshm->wq);
	rshm->rshm_evt_d = NULL;
	init_waitqueue_head(&rshm->waiting_to_free);

	return rshm;
}

static void rshm_disconnect(struct rshmback_t *rshm)
{
	if(atomic_read(&rshm->refcnt) <= 0)
		return ;

	if (rshm->rshm_evt_d) {
		kthread_stop(rshm->rshm_evt_d);
		rshm->rshm_evt_d = NULL;
	}

	atomic_dec(&rshm->refcnt);
	wait_event(rshm->waiting_to_free, atomic_read(&rshm->refcnt) == 0);
	atomic_inc(&rshm->refcnt);

	if (rshm->irq) {
		unbind_from_irqhandler(rshm->irq, rshm);
		rshm->irq = 0;
	}
	
	if (rshm->ring.sring) {
		unmap_ring_page(rshm);
		free_vm_area(rshm->ring_area);
		rshm->ring.sring = NULL;
	}

#ifdef GNT_DEBUG
	printk("rshm shared ring disconnected (%s)\n", rshm->be->dev->nodename);
#endif
}

static int rshmback_probe(struct xenbus_device *xbdev,
			 const struct xenbus_device_id *id)
{
	int err;
	struct backend_info *be = kzalloc(sizeof(struct backend_info), GFP_KERNEL);
	
	if (!be) {
		xenbus_dev_fatal(xbdev, -ENOMEM,
				 "allocating backend structure");
		return -ENOMEM;
	}
	be->dev = xbdev;
	dev_set_drvdata(&xbdev->dev, be);

	be->rshm = rshm_alloc(xbdev->otherend_id);
	if (IS_ERR(be->rshm)) {
		err = PTR_ERR(be->rshm);
		be->rshm = NULL;
		xenbus_dev_fatal(xbdev, err, "creating shared memory");
		goto fail;
	}

	be->rshm->be = be;

	err = xenbus_switch_state(xbdev, XenbusStateInitWait);
	if (err)
		goto fail;

	printk("rshm backend driver proved (%s)\n", xbdev->nodename);

	return 0;

fail:
	printk("rshmback_probe() : failed\n");
	rshmback_remove(xbdev);
	return err;
}

static void rshmback_free(struct rshmback_t *rshm)
{
	if (!atomic_dec_and_test(&rshm->refcnt))
		BUG();

	kfree(rshm);
}

static int rshmback_remove(struct xenbus_device *xbdev)
{
	struct backend_info *be = dev_get_drvdata(&xbdev->dev);

	if(!be)
		return 0;

	dev_dbg(&xbdev->dev, "%s removed", xbdev->nodename);

	if (be->rshm) {
		rshm_disconnect(be->rshm);
		rshmback_free(be->rshm);
		be->rshm = NULL;
	}

	kfree(be);
	dev_set_drvdata(&xbdev->dev, NULL);

	printk("rshm backend driver removed (%s)\n", xbdev->nodename);
	
	return 0;
}

// --------- Driver Registration -----------

static const struct xenbus_device_id rshmback_ids[] = {
	{ "rshm" },
	{ "" }
};


static struct xenbus_driver rshmback = {
	.name = "rshm",
	.owner = THIS_MODULE,
	.ids = rshmback_ids,
	.probe = rshmback_probe,
	.remove = rshmback_remove,
	.otherend_changed = frontend_changed
};


int rshm_back_init(void)
{
	return xenbus_register_backend(&rshmback);
}

static void __exit rshm_back_exit(void)
{
	return xenbus_unregister_driver(&rshmback);
}

module_param(size_kb, ulong, 0);
MODULE_PARM_DESC(size_kb, "Disk size in KB");

module_init(rshm_back_init);
module_exit(rshm_back_exit);

MODULE_LICENSE("Dual BSD/GPL");

