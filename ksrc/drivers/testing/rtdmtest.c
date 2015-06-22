/*
 * Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/mutex.h>

#include <rtdm/rttesting.h>
#include <rtdm/rtdm_driver.h>

struct rtdmtest_context {
	rtdm_event_t event;
	rtdm_sem_t sem;
	rtdm_mutex_t mutex;
	rtdm_nrtsig_t nrtsig;
	struct mutex nrt_mutex;
};

static rtdm_task_t task;
static nanosecs_rel_t task_period;
static unsigned int start_index;
static unsigned long rtdm_lock_count;

#if 0
module_param(start_index, uint, 0400);
MODULE_PARM_DESC(start_index, "First device instance number to be used");
#endif

static int iter = 0;
module_param(iter, int, 0644);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wolfgang Grandegger");

static void rtdmtest_nrtsig_handler(rtdm_nrtsig_t *nrt_sig)
{
	printk("rtdm_nrtsig_handler called\n");
}

static int rtdmtest_task(void *arg)
{
	int ret;
	nanosecs_abs_t wakeup;
	/* struct rttst_rtdmtest_config *config = */
	/* 	(struct rttst_rtdmtest_config *)arg; */

	printk("%s: started with delay=%lld\n", __FUNCTION__, task_period);
        wakeup = rtdm_clock_read();

	while (1) {
	    iter++;
#if 0
		if ((ret = rtdm_task_sleep(task_period)))
			break;
#else
		wakeup += task_period;
		if ((ret = rtdm_task_sleep_until(wakeup)))
			break;
#endif
		//printk("%s: time=%lld\n", __FUNCTION__, rtdm_clock_read());
	}
	printk("%s terminating, ret=%d\n", __FUNCTION__, ret);
	return ret;
}

static int rtdmtest_open(struct rtdm_dev_context *context,
			 rtdm_user_info_t *user_info, int oflags)
{
	struct rtdmtest_context *ctx;

	ctx = (struct rtdmtest_context *)context->dev_private;

	ctx->event.state = 0;
	rtdm_event_init(&ctx->event, 0);
	rtdm_sem_init(&ctx->sem, 0);
	rtdm_lock_count = 0;
	rtdm_mutex_init(&ctx->mutex);
	mutex_init(&ctx->nrt_mutex);
	if (rtdm_nrtsig_init(&ctx->nrtsig, rtdmtest_nrtsig_handler)) {
	    printk("rtdm_nrtsig_init failed\n");
	    return -EINVAL;
	}

	return 0;
}

static int rtdmtest_close(struct rtdm_dev_context *context,
			     rtdm_user_info_t *user_info)
{
	struct rtdmtest_context *ctx;

	ctx = (struct rtdmtest_context *)context->dev_private;
	printk("%s state=%#lx\n", __FUNCTION__, ctx->event.state);
	mutex_lock(&ctx->nrt_mutex);
	rtdm_event_destroy(&ctx->event);
	rtdm_sem_destroy(&ctx->sem);
	rtdm_mutex_destroy(&ctx->mutex);
	mutex_unlock(&ctx->nrt_mutex);

	return 0;
}

static int rtdmtest_ioctl(struct rtdm_dev_context *context,
			  rtdm_user_info_t *user_info,
			  unsigned int request,
			  void *arg)
{
	struct rtdmtest_context *ctx;
	struct rttst_rtdmtest_config config_buf, *config;
	rtdm_toseq_t toseq_local, *toseq = NULL;
	int i, err = 0;

	ctx = (struct rtdmtest_context *)context->dev_private;

	switch (request) {
	case RTTST_RTIOC_RTDMTEST_SEM_TIMEDDOWN:
	case RTTST_RTIOC_RTDMTEST_EVENT_TIMEDWAIT:
        case RTTST_RTIOC_RTDMTEST_MUTEX_TIMEDTEST:
        case RTTST_RTIOC_RTDMTEST_MUTEX_TEST:
		config = arg;
		if (user_info) {
			if (rtdm_safe_copy_from_user
			    (user_info, &config_buf, arg,
			     sizeof(struct rttst_rtdmtest_config)) < 0)
				return -EFAULT;

			config = &config_buf;
		}
		if (!config->seqcount)
			config->seqcount = 1;
		if (config->timeout && config->seqcount > 1) {
			toseq = &toseq_local;
			rtdm_toseq_init(toseq, config->timeout);
		}
		switch(request) {
		case RTTST_RTIOC_RTDMTEST_SEM_TIMEDDOWN:
			for (i = 0; i < config->seqcount; i++) {
				err = rtdm_sem_timeddown(&ctx->sem,
							 config->timeout,
							 toseq);
				if (err)
					break;
			}
			break;
		case RTTST_RTIOC_RTDMTEST_EVENT_TIMEDWAIT:
			for (i = 0; i < config->seqcount; i++) {
				err = rtdm_event_timedwait(&ctx->event,
							   config->timeout,
							   toseq);
				if (err)
					break;
			}
			break;
		case RTTST_RTIOC_RTDMTEST_MUTEX_TIMEDTEST:
			for (i = 0; i < config->seqcount; i++) {
				err = rtdm_mutex_timedlock(&ctx->mutex,
							   config->timeout,
							   toseq);
				if (err)
					break;
				if (config->delay_jiffies) {
					__set_current_state(TASK_INTERRUPTIBLE);
					schedule_timeout(config->delay_jiffies);
				}
				rtdm_lock_count++;
				rtdm_mutex_unlock(&ctx->mutex);
			}
			break;
		case RTTST_RTIOC_RTDMTEST_MUTEX_TEST:
			for (i = 0; i < config->seqcount; i++) {
				if ((err = rtdm_mutex_lock(&ctx->mutex)))
					break;
				rtdm_lock_count++;
				rtdm_mutex_unlock(&ctx->mutex);
			}
			break;
		}
		break;

	case RTTST_RTIOC_RTDMTEST_SEM_DOWN:
		err = rtdm_sem_down(&ctx->sem);
		break;

	case RTTST_RTIOC_RTDMTEST_SEM_UP:
		rtdm_sem_up(&ctx->sem);
		break;

        case RTTST_RTIOC_RTDMTEST_SEM_DESTROY:
                rtdm_sem_destroy(&ctx->sem);
                break;

	case RTTST_RTIOC_RTDMTEST_EVENT_WAIT:
		err = rtdm_event_wait(&ctx->event);
		break;

	case RTTST_RTIOC_RTDMTEST_EVENT_SIGNAL:
		rtdm_event_signal(&ctx->event);
		break;

        case RTTST_RTIOC_RTDMTEST_EVENT_DESTROY:
                rtdm_event_destroy(&ctx->event);
                break;

        case RTTST_RTIOC_RTDMTEST_MUTEX_DESTROY:
                rtdm_mutex_destroy(&ctx->mutex);
                break;

        case RTTST_RTIOC_RTDMTEST_MUTEX_GETSTAT:
		printk("RTTST_RTIOC_RTDMTEST_MUTEX_GETSTAT\n");
		if (user_info)
			config = &config_buf;
		else
			config = arg;
		config->seqcount = rtdm_lock_count;
		if (user_info) {
			if (rtdm_safe_copy_to_user
			    (user_info, arg, &config_buf,
			     sizeof(struct rttst_rtdmtest_config)) < 0)
				return -EFAULT;
		}
                break;

        case RTTST_RTIOC_RTDMTEST_NRTSIG_PEND:
		rtdm_nrtsig_pend(&ctx->nrtsig);
                break;

        case RTTST_RTIOC_RTDMTEST_TASK_CREATE:
        case RTTST_RTIOC_RTDMTEST_TASK_SET_PRIO:
                config = arg;
                if (user_info) {
                        if (rtdm_safe_copy_from_user
                            (user_info, &config_buf, arg,
                             sizeof(struct rttst_rtdmtest_config)) < 0)
                                return -EFAULT;

                        config = &config_buf;
                }
		if (request == RTTST_RTIOC_RTDMTEST_TASK_CREATE) {
			task_period = config->timeout;
			rtdm_task_init(&task, "RTDMTEST",
				       rtdmtest_task, (void *)config,
				       config->priority, 0);
		} else {
			rtdm_task_set_priority(&task, config->priority);
		}
		break;

        case RTTST_RTIOC_RTDMTEST_TASK_DESTROY:
		rtdm_task_destroy(&task);
		rtdm_task_join_nrt(&task, 100);
                break;

	default:
		printk("request=%d\n", request);
		err = -ENOTTY;
	}

	return err;
}

static struct rtdm_device device = {
	.struct_version    = RTDM_DEVICE_STRUCT_VER,

	.device_flags      = RTDM_NAMED_DEVICE,
	.context_size      = sizeof(struct rtdmtest_context),
	.device_name       = "",

	.open_nrt = rtdmtest_open,

	.ops = {
		.close_nrt = rtdmtest_close,
		.ioctl_rt  = rtdmtest_ioctl,
	},

	.device_class      = RTDM_CLASS_TESTING,
	.device_sub_class  = RTDM_SUBCLASS_RTDMTEST,
	.driver_name       = "xeno_rtdmtest",
	.driver_version    = RTDM_DRIVER_VER(0, 1, 1),
	.peripheral_name   = "Basic RTDM testsuite",
	.provider_name     = "Wolfgang Grandegger",
	.proc_name         = device.device_name,
};

static int __init __rtdmtest_init(void)
{
	int err;

	do {
		snprintf(device.device_name, RTDM_MAX_DEVNAME_LEN,
			 "rttest%d", start_index);
		err = rtdm_dev_register(&device);
		printk("%s: registering device %s, err=%d\n",
		       __FUNCTION__, device.device_name, err);

		start_index++;
	} while (err == -EEXIST);

	return err < 0 ? err : 0;
}

static void __exit __rtdmtest_exit(void)
{
	rtdm_task_destroy(&task);
	rtdm_task_join_nrt(&task, 100);
	printk("%s: unregistering device %s\n",
	       __FUNCTION__, device.device_name);
	rtdm_dev_unregister(&device, 1000);
}

module_init(__rtdmtest_init);
module_exit(__rtdmtest_exit);
