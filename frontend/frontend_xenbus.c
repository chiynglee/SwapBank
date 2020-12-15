#include <linux/module.h>

#include "common.h"
#include "shalloc.h"

#include "compcache-domu/ramzswap_drv.h"

static int sync_xenbus_state(struct xenbus_device *xbdev);

static int do_rshm_fe_op(struct rshmfront_info_t *info);
static void connect_ring_evt(struct rshmfront_info_t *info);

static int get_id_from_freelist(struct rshmfront_info_t *info);
static void add_id_to_freelist(struct rshmfront_info_t *info, unsigned long id);

static void rshmfront_free(struct rshmfront_info_t *info);
static int setup_ring(struct xenbus_device *xbdev,
			 struct rshmfront_info_t *info);
static int talk_to_rshmback(struct xenbus_device *dev,
			   struct rshmfront_info_t *info);

static int rshmfront_probe(struct xenbus_device *xbdev,
			  const struct xenbus_device_id *id);
static int rshmfront_remove(struct xenbus_device *xbdev);
static int rshmfront_resume(struct xenbus_device *xbdev);
static void rshmback_changed(struct xenbus_device *xbdev, 
			enum xenbus_state backend_state);


// Module params (documentation at end)
static unsigned int num_devs;
static unsigned long size_kb;
static unsigned long limit_kb;
static char backing[MAX_SWAP_NAME_LEN];


unsigned long get_size_of_swapspace(void)
{
	// default size = 50MB
	if(size_kb == 0)
		return 50*1024;
	else
		return size_kb;
}

static int get_id_from_freelist(struct rshmfront_info_t *info)
{
	unsigned long free = info->free_id;
	BUG_ON(free >= RSHM_RING_SIZE);
	info->free_id = info->sended_reqs[free].id;
	info->sended_reqs[free].id = 0x0fffffee;	//debug
	return free;
}

static void add_id_to_freelist(struct rshmfront_info_t *info, unsigned long id)
{
	info->sended_reqs[id].id  = info->free_id;
	info->free_id = id;
}

void send_requests(struct rshmfront_info_t *info)
{
	int notify;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&info->ring, notify);

	if (notify)
		notify_remote_via_irq(info->irq);
}

void create_rquest(struct rshmfront_info_t *info, 
			uint8_t op, uint32_t ref, uint16_t offset, uint8_t lvl) 
{
	struct rshm_request_t *req;
	int id;

	spin_lock_irq(&info->ring_lock);

	req = RING_GET_REQUEST(&info->ring, info->ring.req_prod_pvt);

	id = get_id_from_freelist(info);

	req->id = id;
	req->operation = op;
	req->ref = ref;
	req->offset = offset;
	req->map_lvl = lvl;

	memcpy(&info->sended_reqs[id], req, sizeof(req));

	info->ring.req_prod_pvt++;

	spin_unlock_irq(&info->ring_lock);
}

static void rshmfront_free(struct rshmfront_info_t *info)
{
	if (info->rshm_evt_d) {
		kthread_stop(info->rshm_evt_d);
		info->rshm_evt_d = NULL;
	}

	if (info->irq)
		unbind_from_irqhandler(info->irq, info);
	info->evtchn = info->irq = 0;

	spin_lock_irq(&info->ring_lock);

	if (info->ring_ref != INVALID_REF) {
		gnttab_end_foreign_access(info->ring_ref, RW, (unsigned long)info->ring.sring);
		info->ring_ref = INVALID_REF;
		info->ring.sring = NULL;
	}

	spin_unlock_irq(&info->ring_lock);

#ifdef GNT_DEBUG
	printk("rshm frontend driver freed\n");
#endif

}

static void connect_ring_evt(struct rshmfront_info_t *info)
{
	int err;
	char name[TASK_COMM_LEN];

	// Not ready to connect?
	if (!info->irq)
		return;

	// Already connected?
	if (info->rshm_evt_d)
		return;

	xenbus_switch_state(info->xbdev, XenbusStateConnected);

	snprintf(name, TASK_COMM_LEN, "rshmfront.to.%d", info->xbdev->otherend_id);

	info->rshm_evt_d = kthread_run(rshm_fe_schedule, info, name);
	if (IS_ERR(info->rshm_evt_d)) {
		err = PTR_ERR(info->rshm_evt_d);
		info->rshm_evt_d = NULL;
		xenbus_dev_error(info->xbdev, err, "start rshm_evt_d");
	}
}

int rshm_fe_schedule(void *arg)
{
	struct rshmfront_info_t *info = arg;

#ifdef GNT_DEBUG
	printk(KERN_DEBUG "%s: started\n", current->comm);
#endif

	while (!kthread_should_stop()) {
		if (try_to_freeze())
			continue;

		wait_event_interruptible(
			info->wq,
			info->waiting_msgs || kthread_should_stop());

		info->waiting_msgs = 0;
		smp_mb(); // clear flag

		if (do_rshm_fe_op(info))
			info->waiting_msgs = 1;
	}

#ifdef GNT_DEBUG
	printk(KERN_DEBUG "%s: exiting\n", current->comm);
#endif

	info->rshm_evt_d = NULL;
	
	return 0;
}

irqreturn_t rshm_fe_interrupt(int irq, void *dev_id)
{
	struct rshmfront_info_t *info;

	info = (struct rshmfront_info_t *) dev_id;
	info->waiting_msgs = 1;
	wake_up(&info->wq);

	return IRQ_HANDLED;
}

static int do_rshm_fe_op(struct rshmfront_info_t *info)
{
	struct rshm_response_t *resp;
	RING_IDX cons, rp;
	int error = 0;
	int more_to_do = 0;

	cons = info->ring.rsp_cons;
	rp = info->ring.sring->rsp_prod;
	rmb(); // Ensure we see queued responses up to 'rp'.

	while(cons != rp) {
		if (kthread_should_stop()) {
			more_to_do = 1;
			break;
		}

		resp = RING_GET_RESPONSE(&info->ring, cons);
		info->ring.rsp_cons = ++cons;

		add_id_to_freelist(info, resp->id);

		error = (resp->status == RSP_OKAY) ? 0 : -EIO;
		if(error < 0) {
			printk(KERN_WARNING "rshmfront: %s: response message is error (%d)\n", info->xbdev->nodename, resp->status);
			continue;
		}
		
		switch (resp->operation) {
		case RESP_MAP:
			if(resp->ref == INVALID_REF && resp->offset == INVALID_OFFSET) {
				error = mapping_space_bw_doms(info->xbdev);
				if(error < 0) {
					printk(KERN_WARNING "rshmfront: %s: remote mapping is fail\n", info->xbdev->nodename);
				}
				else {
					if(!shcomp_is_ready()) {
						printk("shcomp_control: shcomp is not ready\n");
						return -1;
					}

					printk("num_devs = %d, size_kb = %ld, limit_kb = %ld, backing = %s\n", num_devs, size_kb, limit_kb, backing);
					ramzswap_init(num_devs, size_kb, limit_kb, backing);		// ramzswap

//					initialize_shcomp();			// shalloc_test
//					shcomp_alloc_loop_test(10);		// shalloc_test

				}
			}

			break;
		case RESP_UNMAP:
			if(resp->ref == INVALID_REF && resp->offset == INVALID_OFFSET) {
				printk(KERN_WARNING "rshmfront: %s: remote space removed\n", info->xbdev->nodename);

				if(sync_xenbus_state(info->xbdev) < 0) {
					printk(KERN_WARNING "rshmfront: %s: state synchronization failed\n", info->xbdev->nodename);
				}

				if(info->xbdev->state == XenbusStateClosing) {
					xenbus_switch_state(info->xbdev, XenbusStateClosed);
				}
			}
			break;
		case RESP_FIND:
			break;
		default:
			BUG();
		}

		cond_resched();
	}

	if (cons != info->ring.req_prod_pvt) {
		RING_FINAL_CHECK_FOR_RESPONSES(&info->ring, more_to_do);
	}
	else
		info->ring.sring->rsp_event = cons + 1;

	return more_to_do;
}

static int sync_xenbus_state(struct xenbus_device *xbdev)
{
	struct xenbus_transaction xbt;
	int err;
	int current_state;
	int abort = 0;

	err = xenbus_transaction_start(&xbt);
	if (err) {
		printk("rshmfront: %s: starting transaction failed (%d)\n", xbdev->nodename, err);
		return -1;
	}

	err = xenbus_scanf(xbt, xbdev->nodename, "state", "%d", &current_state);
	if (err != 1)
		abort = 1;

	err = xenbus_transaction_end(xbt, abort);
	if (err) {
		printk("rshmfront: %s: ending transaction failed (%d)\n", xbdev->nodename, err);
		return -1;
	}

	if(abort) {
		printk("rshmfront: %s: reading state failed (%d)\n", xbdev->nodename, err);
		return -2;
	}

	if(xbdev->state != current_state)
		xbdev->state = current_state;

	return 0;
}

static int setup_ring(struct xenbus_device *xbdev,
			 struct rshmfront_info_t *info)
{
	struct rshm_sring *sring;
	int err;

	info->ring_ref = INVALID_REF;

	sring = (struct rshm_sring *)__get_free_page(GFP_NOIO | __GFP_HIGH);
	if (!sring) {
		xenbus_dev_fatal(xbdev, -ENOMEM, "allocating shared ring");
		return -ENOMEM;
	}
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&info->ring, sring, PAGE_SIZE);

	err = xenbus_grant_ring(xbdev, virt_to_mfn(info->ring.sring));
	if (err < 0) {
		free_page((unsigned long)sring);
		info->ring.sring = NULL;
		goto fail;
	}
	info->ring_ref = err;
	
	err = xenbus_alloc_evtchn(xbdev, &info->evtchn);
	if (err)
		goto fail;

	err = bind_evtchn_to_irqhandler(info->evtchn,
					rshm_fe_interrupt,
					IRQF_SAMPLE_RANDOM, "rshm-frontend", info);
	if (err <= 0) {
		xenbus_dev_fatal(xbdev, err,
				 "bind_evtchn_to_irqhandler failed");
		goto fail;
	}
	info->irq = err;

#ifdef GNT_DEBUG
	printk("rshm shared ring setup (%s)\n", xbdev->nodename);
#endif

	return 0;
fail:
	printk(" setup_ring () fail\n");
	
	rshmfront_free(info);
	return err;
}

static int talk_to_rshmback(struct xenbus_device *dev,
			   struct rshmfront_info_t *info)
{
	const char *message = NULL;
	struct xenbus_transaction xbt;
	int err;

	// Create shared ring, alloc event channel.
	err = setup_ring(dev, info);
	if (err)
		goto out;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto destroy_ring;
	}

	err = xenbus_printf(xbt, dev->nodename,
			    "ring-ref", "%u", info->ring_ref);
	if (err) {
		message = "writing ring-ref";
		goto abort_transaction;
	}
	err = xenbus_printf(xbt, dev->nodename,
			    "event-channel", "%u", info->evtchn);
	if (err) {
		message = "writing event-channel";
		goto abort_transaction;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto destroy_ring;
	}

	xenbus_switch_state(dev, XenbusStateInitialised);

	return 0;

 abort_transaction:
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_fatal(dev, err, "%s", message);
 destroy_ring:
	rshmfront_free(info);
 out:
	return err;
}

static int rshmfront_probe(struct xenbus_device *xbdev,
			  const struct xenbus_device_id *id)
{
	int err;
	int i;
	struct rshmfront_info_t *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		xenbus_dev_fatal(xbdev, -ENOMEM, "allocating info structure");
		return -ENOMEM;
	}

	spin_lock_init(&info->ring_lock);
	info->xbdev = xbdev;
	init_waitqueue_head(&info->wq);
	info->rshm_evt_d = NULL;

	for (i = 0; i < RSHM_RING_SIZE; i++)
		info->sended_reqs[i].id = i+1;
	info->sended_reqs[RSHM_RING_SIZE-1].id = 0x0fffffff;

	// Front end dir is a number, which is used as the id.
	info->handle = simple_strtoul(strrchr(xbdev->nodename, '/')+1, NULL, 0);
	dev_set_drvdata(&xbdev->dev, info);

	err = talk_to_rshmback(xbdev, info);
	if (err) {
		kfree(info);
		dev_set_drvdata(&xbdev->dev, NULL);
		return err;
	}

#ifdef GNT_DEBUG
	printk("rshm frontend driver probed (%s)\n", xbdev->nodename);
#endif

	return 0;
}

static int rshmfront_remove(struct xenbus_device *xbdev)
{
	struct rshmfront_info_t *info = dev_get_drvdata(&xbdev->dev);

	if(!info)
		return 0;
	
	dev_dbg(&xbdev->dev, "%s removed", xbdev->nodename);

	rshmfront_free(info);

	info->xbdev = NULL;
	kfree(info);
	dev_set_drvdata(&xbdev->dev, NULL);

#ifdef GNT_DEBUG
	printk("rshm frontend driver removed (%s)\n", xbdev->nodename);
#endif

	return 0;
}

static int rshmfront_resume(struct xenbus_device *xbdev)
{
	struct rshmfront_info_t *info = dev_get_drvdata(&xbdev->dev);
	int err;

	dev_dbg(&xbdev->dev, "rshmfront_resume: %s\n", xbdev->nodename);

	rshmfront_free(info);

	err = talk_to_rshmback(xbdev, info);

#ifdef GNT_DEBUG
	printk("rshm frontend driver resumed (%s)\n", xbdev->nodename);
#endif

	return err;
}

static void rshmback_changed(struct xenbus_device *xbdev,
			    enum xenbus_state backend_state)
{
	struct rshmfront_info_t *info = dev_get_drvdata(&xbdev->dev);

	dev_dbg(&xbdev->dev, "rshmfront:rshmback_changed to state %d.\n", backend_state);

#ifdef GNT_DEBUG
	printk("\n backend_changed() start : be-state = %d fe-state = %d\n", backend_state, xbdev->state);
#endif

	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateInitWait:
	case XenbusStateInitialised:
	case XenbusStateUnknown:
		break;
	case XenbusStateClosed:
		xenbus_frontend_closed(info->xbdev);
		rshmfront_remove(xbdev);
		break;
	case XenbusStateConnected:
		connect_ring_evt(info);

		create_rquest(info, REQ_MAP, INVALID_REF, INVALID_OFFSET, 2);
		send_requests(info);
		
		break;
	case XenbusStateClosing:
		ramzswap_exit();		// ramzswap
//		remove_shcomp();	// shalloc_test

		unmapping_space_bw_doms();

		create_rquest(info, REQ_UNMAP, INVALID_REF, INVALID_OFFSET, 2);
		send_requests(info);
		
		break;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32))
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
		break;
#endif
	}
}

static const struct xenbus_device_id rshmfront_ids[] = {
	{ "rshm" },
	{ "" }
};

static struct xenbus_driver rshmfront = {
	.name = "rshm",
	.owner = THIS_MODULE,
	.ids = rshmfront_ids,
	.probe = rshmfront_probe,
	.remove = rshmfront_remove,
	.resume = rshmfront_resume,
	.otherend_changed = rshmback_changed,
};

static int __init rshm_front_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	return xenbus_register_frontend(&rshmfront);
}

static void __exit rshm_front_exit(void)
{
	return xenbus_unregister_driver(&rshmfront);
}

module_param(num_devs, uint, 0);
MODULE_PARM_DESC(num_devs, "Number of ramzswap devices");

module_param(size_kb, ulong, 0);
MODULE_PARM_DESC(size_kb, "Disksize in KB");

module_param(limit_kb, ulong, 0);
MODULE_PARM_DESC(limit_kb, "Memlimit in KB");

module_param_string(backing, backing, sizeof(backing), 0);
MODULE_PARM_DESC(backing, "Backing swap name");

module_init(rshm_front_init);
module_exit(rshm_front_exit);

MODULE_LICENSE("Dual BSD/GPL");

