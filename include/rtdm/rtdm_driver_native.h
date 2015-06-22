/*
 * Real-Time Driver Model for native Linux-rt, driver API header
 *
 * Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2005, 2006 Jan Kiszka <jan.kiszka@web.de>
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
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

#ifndef _RTDM_DRIVER_NATIVE_H
#define _RTDM_DRIVER_NATIVE_H

#include <asm/atomic.h>
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <asm/bitops.h>
#include <linux/net.h>
#include <asm/errno.h>
#include <net/sock.h>

#include <rtdm/wrappers_native.h>
#include <rtdm/rtdm.h>

struct rtdm_dev_context;

/*
 * Static flags describing a RTDM device
 */

/** If set, only a single instance of the device can be requested by an
 *  application. */
#define RTDM_EXCLUSIVE              0x0001

/** If set, the device is addressed via a clear-text name. */
#define RTDM_NAMED_DEVICE           0x0010

/** If set, the device is addressed via a combination of protocol ID and
 *  socket type. */
#define RTDM_PROTOCOL_DEVICE        0x0020

/** Mask selecting the device type. */
#define RTDM_DEVICE_TYPE_MASK       0x00F0

/*
 * Dynamic flags describing the state of an open RTDM device (bit numbers)
 */

/** Set by RTDM if the device instance was created in non-real-time
 *  context. */
#define RTDM_CREATED_IN_NRT         0

/** Set by RTDM when the device is being closed. */
#define RTDM_CLOSING                1

/** Set by RTDM if the device has to be closed regardless of possible pending
 *  locks held by other users. */
#define RTDM_FORCED_CLOSING         2

/** Lowest bit number the driver developer can use freely */
#define RTDM_USER_CONTEXT_FLAG      8	/* first user-definable flag */

/*
 * Current revisions of RTDM structures, encoding of driver versions.
 */

#define RTDM_DEVICE_STRUCT_VER      3

#define RTDM_CONTEXT_STRUCT_VER     3

#define RTDM_SECURE_DEVICE          0x80000000

#define RTDM_DRIVER_VER(major, minor, patch) \
    (((major & 0xFF) << 16) | ((minor & 0xFF) << 8) | (patch & 0xFF))

#define RTDM_DRIVER_MAJOR_VER(ver)  (((ver) >> 16) & 0xFF)

#define RTDM_DRIVER_MINOR_VER(ver)  (((ver) >> 8) & 0xFF)

#define RTDM_DRIVER_PATCH_VER(ver)  ((ver) & 0xFF)

/*
 * Operation Handler Prototypes
 */

typedef
int (*rtdm_open_handler_t) (struct rtdm_dev_context * context,
			    rtdm_user_info_t * user_info, int oflag);

typedef
int (*rtdm_socket_handler_t) (struct rtdm_dev_context * context,
			      rtdm_user_info_t * user_info, int protocol);

typedef
int (*rtdm_close_handler_t) (struct rtdm_dev_context * context,
			     rtdm_user_info_t * user_info);

typedef
int (*rtdm_ioctl_handler_t) (struct rtdm_dev_context * context,
			     rtdm_user_info_t * user_info,
			     unsigned int request, void *arg);

typedef
ssize_t(*rtdm_read_handler_t) (struct rtdm_dev_context * context,
			       rtdm_user_info_t * user_info,
			       void *buf, size_t nbyte);

typedef
ssize_t(*rtdm_write_handler_t) (struct rtdm_dev_context * context,
				rtdm_user_info_t * user_info,
				const void *buf, size_t nbyte);

typedef
ssize_t(*rtdm_recvmsg_handler_t) (struct rtdm_dev_context * context,
				  rtdm_user_info_t * user_info,
				  struct msghdr * msg, int flags);

typedef
ssize_t(*rtdm_sendmsg_handler_t) (struct rtdm_dev_context * context,
				  rtdm_user_info_t * user_info,
				  const struct msghdr * msg, int flags);

typedef
int (*rtdm_rt_handler_t) (struct rtdm_dev_context * context,
			  rtdm_user_info_t * user_info, void *arg);

/*
 * Device operations
 */
struct rtdm_operations {
	/* Common Operations */
	rtdm_close_handler_t close_rt;
	rtdm_close_handler_t close_nrt;
	rtdm_ioctl_handler_t ioctl_rt;
	rtdm_ioctl_handler_t ioctl_nrt;
	/* Stream-Oriented Device Operations */
	rtdm_read_handler_t read_rt;
	rtdm_read_handler_t read_nrt;
	rtdm_write_handler_t write_rt;
	rtdm_write_handler_t write_nrt;
	/* Message-Oriented Device Operations */
	rtdm_recvmsg_handler_t recvmsg_rt;
	rtdm_recvmsg_handler_t recvmsg_nrt;
	rtdm_sendmsg_handler_t sendmsg_rt;
	rtdm_sendmsg_handler_t sendmsg_nrt;
};

/*
 * Device context
 */
struct rtdm_dev_context {
	struct sock sk_lx;
	unsigned long context_flags;
	int fd;
	atomic_t close_lock_count;
	struct rtdm_operations *ops;
	struct rtdm_device *device;
	char dev_private[0];
};

struct rtdm_dev_reserved {
	int chrdev_major;
	atomic_t refcount;
	struct file_operations chrdev_ops;
	struct net_proto_family proto_family;
	struct proto_ops proto_ops;
	struct proto proto;
};

/*
 * RTDM device
 */
struct rtdm_device {
	struct list_head list;

	int struct_version;
	int device_flags;
	size_t context_size;
	char device_name[RTDM_MAX_DEVNAME_LEN + 1];
	int protocol_family;
	int socket_type;
	rtdm_open_handler_t open_rt;
	rtdm_open_handler_t open_nrt;
	rtdm_socket_handler_t socket_rt;
	rtdm_socket_handler_t socket_nrt;
	/** Protocol socket creating for RTDM over Linux */
        int (*socket_lx)(struct net *net, struct socket *sock, int protocol, int kern);
	struct rtdm_operations ops;
	int device_class;
	int device_sub_class;
	int profile_version;
	const char *driver_name;
	int driver_version;
	const char *peripheral_name;
	const char *provider_name;
	const char *proc_name;
	struct proc_dir_entry *proc_entry;
	int device_id;
	/* Data stored by RTDM inside a registered device (internal use only) */
	struct rtdm_dev_reserved reserved;
};

int rtdm_socket_lx(struct socket *sock, int protocol,
		   struct rtdm_device *device);

/*
 * Device registration
 */
int rtdm_dev_register(struct rtdm_device *device);
int rtdm_dev_unregister(struct rtdm_device *device, unsigned int poll_delay);

struct rtdm_dev_context *rtdm_context_get(int fd);

static inline void rtdm_context_lock(struct rtdm_dev_context *context)
{
	atomic_inc(&context->close_lock_count);
}

static inline void rtdm_context_unlock(struct rtdm_dev_context *context)
{
	atomic_dec(&context->close_lock_count);
}

/*
 * Clock services
 */
static inline nanosecs_abs_t rtdm_clock_read(void)
{
	return ktime_to_ns(ktime_get_real());
}

/*
 * Spin lock services
 */

/** Global lock across scheduler invocation */

/* FIXME */
#define RTDM_EXECUTE_ATOMICALLY(code_block) \
{ \
	code_block \
}

/*
 * Spinlock with preemption deactivation
 */

#define RTDM_LOCK_UNLOCKED          SPIN_LOCK_UNLOCKED

typedef spinlock_t rtdm_lock_t;

typedef unsigned long rtdm_lockctx_t;

#define rtdm_lock_init(lock)        spin_lock_init(lock)

#define rtdm_lock_get(lock)         spin_lock(lock)

#define rtdm_lock_put(lock)         spin_unlock(lock)

#define rtdm_lock_get_irqsave(lock, context)    \
	spin_lock_irqsave(lock, context)

#define rtdm_lock_put_irqrestore(lock, context) \
	spin_unlock_irqrestore(lock, context)

#define rtdm_lock_irqsave(context)              \
	local_irqsave(context)

#define rtdm_lock_irqrestore(context)           \
	local_irqrestore(context)

/*
 * Interrupt management services
 */

typedef struct rtdm_irq {
	int irq_no;
	void *cookie;
	int (*handler)(struct rtdm_irq * irq_handle);
} rtdm_irq_t;

#define RTDM_IRQTYPE_SHARED         IRQF_SHARED
#define RTDM_IRQTYPE_EDGE           0

#define RTDM_IRQ_NONE               IRQ_NONE
#define RTDM_IRQ_HANDLED            IRQ_HANDLED

typedef int (*rtdm_irq_handler_t) (rtdm_irq_t * irq_handle);

#define rtdm_irq_get_arg(irq_handle, type)  ((type *)irq_handle->cookie)

int rtdm_irq_request(rtdm_irq_t * irq_handle,
		     unsigned int irq_no,
		     rtdm_irq_handler_t handler,
		     unsigned long flags,
		     const char *device_name, void *arg);

static inline int rtdm_irq_free(rtdm_irq_t * irq_handle)
{
	free_irq(irq_handle->irq_no, irq_handle);
	return 0;
}

static inline int rtdm_irq_enable(rtdm_irq_t * irq_handle)
{
	enable_irq(irq_handle->irq_no);
	return 0;
}

static inline int rtdm_irq_disable(rtdm_irq_t * irq_handle)
{
	disable_irq(irq_handle->irq_no);
	return 0;
}

/*
 * Non-real-time signalling services
 */

typedef struct work_struct rtdm_nrtsig_t;

typedef void (*rtdm_nrtsig_handler_t) (rtdm_nrtsig_t *nrt_sig);

static inline int rtdm_nrtsig_init(rtdm_nrtsig_t *nrt_sig,
				   rtdm_nrtsig_handler_t handler)
{
	if (handler == NULL)
		return -EINVAL;

	INIT_WORK(nrt_sig, handler);
	return 0;
}

static inline void rtdm_nrtsig_destroy(rtdm_nrtsig_t *nrt_sig)
{
    cancel_work_sync(nrt_sig);  // XXX not sure
}

static inline void rtdm_nrtsig_pend(rtdm_nrtsig_t *nrt_sig)
{
	schedule_work(nrt_sig);
}

/*
 * Task and timing services
 */

typedef int (*rtdm_task_proc_t) (void *arg);

typedef struct {
	unsigned long magic;
	struct task_struct *linux_task;
	struct completion start;
	int priority;
	int stopped;
	rtdm_task_proc_t proc;
	void *arg;
} rtdm_task_t;

#define RTDM_TASK_LOWEST_PRIORITY   0
#define RTDM_TASK_HIGHEST_PRIORITY  MAX_RT_PRIO

#define RTDM_TASK_RAISE_PRIORITY    (+1)
#define RTDM_TASK_LOWER_PRIORITY    (-1)

#define RTDM_TASK_MAGIC             0x07041959

#define rtdm_task_has_magic(task)   (task->magic == RTDM_TASK_MAGIC)

int rtdm_task_init(rtdm_task_t *task, const char *name,
		   rtdm_task_proc_t proc, void *arg,
		   int priority, nanosecs_rel_t period);

static inline void rtdm_task_destroy(rtdm_task_t * task)
{
    	int ret;
	if (rtdm_task_has_magic(task) && !task->stopped) {

#if 0
		printk("%s: sending signal to %s (pid=%d)\n",
		       __FUNCTION__, task->linux_task->comm,
		       task->linux_task->pid);
		send_sig(SIGINT, task->linux_task, 1);
		wake_up_process(task->linux_task);
#else
		printk("%s: stopping kthread %s (pid=%d)\n",
		       __FUNCTION__, task->linux_task->comm,
		       task->linux_task->pid);
		ret = kthread_stop(task->linux_task);
		if (ret == -EINTR)
		    printk("%s: kthread_stop: wake_up_process was never called\n", __FUNCTION__);

#endif
	} else {
		printk("%s: not allowed on user threads\n", __FUNCTION__);
	}
}

static inline void rtdm_task_join_nrt(rtdm_task_t *task,
				      unsigned int poll_delay)
{
        if (rtdm_task_has_magic(task)) {
		printk("%s: poll_delay=%d\n", __FUNCTION__, poll_delay);
		while (!task->stopped)
			msleep(poll_delay);
        } else
                printk("%s: not allowed on user threads\n", __FUNCTION__);
}


void rtdm_task_set_priority(rtdm_task_t *task, int priority);

static inline int rtdm_task_set_period(rtdm_task_t * task,
				       nanosecs_rel_t period)
{
	printk("%s not supported\n", __FUNCTION__);
	return -EOPNOTSUPP;
}

static inline int rtdm_task_unblock(rtdm_task_t *task)
{
	if (rtdm_task_has_magic(task) && !task->stopped) {
		send_sig(SIGINT, task->linux_task, 1);
		return wake_up_process(task->linux_task);
	} else
		return wake_up_process((struct task_struct *)task);
}

static inline rtdm_task_t *rtdm_task_current(void)
{
	return (rtdm_task_t *)current;
}

static inline int rtdm_task_wait_period(void)
{
	printk("%s not yet supported\n", __FUNCTION__);
	return -EOPNOTSUPP;
}

int _rtdm_task_sleep(struct hrtimer_sleeper *sleeper);

static inline int rtdm_task_sleep(nanosecs_rel_t delay)
{
	struct hrtimer_sleeper timeout;
	hrtimer_init(&timeout.timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	hrtimer_init_sleeper(&timeout, current);
	hrtimer_set_expires(&timeout.timer,
			    ktime_add_ns(timeout.timer.base->get_time(),
					 delay));
	return _rtdm_task_sleep(&timeout);
}

static inline int rtdm_task_sleep_until(nanosecs_abs_t wakeup_time)
{
	struct hrtimer_sleeper timeout;
	ktime_t zero = ktime_set(0, 0);
	hrtimer_init(&timeout.timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	hrtimer_init_sleeper(&timeout, current);
	hrtimer_set_expires(&timeout.timer,ktime_add_ns(zero, wakeup_time));
	return _rtdm_task_sleep(&timeout);
}


static inline void rtdm_task_busy_sleep(nanosecs_rel_t delay)
{
	udelay((u32)do_div(delay, 1000));
}

/*
 * Timeout sequences
 */

typedef struct hrtimer_sleeper rtdm_toseq_t;

static inline void rtdm_toseq_init(rtdm_toseq_t *toseq,
				   nanosecs_rel_t timeout)
{
	hrtimer_init(&toseq->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	hrtimer_init_sleeper(toseq, current);
	hrtimer_set_expires(&toseq->timer,
			    ktime_add_ns(toseq->timer.base->get_time(),
					 timeout));
}

/*
 * Event services
 */

typedef struct {
	wait_queue_head_t wait;
	unsigned long state;
} rtdm_event_t;

#define RTDM_EVENT_PENDING   1
#define RTDM_EVENT_DESTROY   2

static inline void rtdm_event_init(rtdm_event_t *event, unsigned long pending)
{
	init_waitqueue_head(&event->wait);
	if (pending)
		set_bit(RTDM_EVENT_PENDING, &event->state);
	else
		clear_bit(RTDM_EVENT_PENDING, &event->state);
	clear_bit(RTDM_EVENT_DESTROY, &event->state);
	smp_mb();
}

static inline void rtdm_event_destroy(rtdm_event_t * event)
{
	set_bit(RTDM_EVENT_DESTROY, &event->state);
	smp_mb();
	wake_up_all(&event->wait);
}

static inline void rtdm_event_pulse(rtdm_event_t * event)
{
	wake_up_all(&event->wait);
}

int _rtdm_event_wait(rtdm_event_t * event);

static inline int rtdm_event_wait(rtdm_event_t *event)
{
	if (test_bit(RTDM_EVENT_DESTROY, &event->state))
		return -EIDRM;
	else if (test_and_clear_bit(RTDM_EVENT_PENDING, &event->state))
		return 0;
	else return _rtdm_event_wait(event);
}

int _rtdm_event_timedwait(rtdm_event_t *event,
			  nanosecs_rel_t timeout,
			  rtdm_toseq_t *toseq);

static inline int rtdm_event_timedwait(rtdm_event_t *event,
				       nanosecs_rel_t timeout,
				       rtdm_toseq_t *toseq)
{
	if (test_bit(RTDM_EVENT_DESTROY, &event->state))
		return -EIDRM;
	else if (test_and_clear_bit(RTDM_EVENT_PENDING, &event->state))
		return 0;
	else return _rtdm_event_timedwait(event, timeout, toseq);
}

static inline void rtdm_event_signal(rtdm_event_t * event)
{
	set_bit(RTDM_EVENT_PENDING, &event->state);
	wake_up(&event->wait);
}

static inline void rtdm_event_clear(rtdm_event_t * event)
{
	clear_bit(RTDM_EVENT_PENDING, &event->state);
}

/*
 * Semaphore services
 */

#define RTDM_SEM_DESTROY 2

typedef struct {
	unsigned long state;
        atomic_t count;
        int sleepers;
        wait_queue_head_t wait;
} rtdm_sem_t;

static inline void rtdm_sem_init(rtdm_sem_t *sem, unsigned long val)
{
	atomic_set(&sem->count, val);
        sem->sleepers = 0;
        init_waitqueue_head(&sem->wait);
	clear_bit(RTDM_EVENT_DESTROY, &sem->state);
	smp_mb();
}

static inline void rtdm_sem_destroy(rtdm_sem_t *sem)
{
	set_bit(RTDM_EVENT_DESTROY, &sem->state);
	wake_up_all(&sem->wait);
}

int _rtdm_sem_timeddown(rtdm_sem_t *sem, nanosecs_rel_t timeout,
			rtdm_toseq_t *toseq);

static inline int rtdm_sem_timeddown(rtdm_sem_t *sem,
				     nanosecs_rel_t timeout,
				     rtdm_toseq_t *toseq)
{
        if (likely(atomic_dec_return(&sem->count) < 0))
		return _rtdm_sem_timeddown(sem, timeout, toseq);
	return 0;
}

int _rtdm_sem_down(rtdm_sem_t *sem);

static inline int rtdm_sem_down(rtdm_sem_t *sem)
{
        if (likely(atomic_dec_return(&sem->count) < 0))
                return _rtdm_sem_down(sem);
	return 0;
}


static inline void rtdm_sem_up(rtdm_sem_t *sem)
{
        if (likely(atomic_inc_return(&sem->count) <= 0))
		wake_up(&sem->wait);
}

/*
 * Mutex services
 */

typedef struct {
    unsigned long state;
    struct rt_mutex lock;
} rtdm_mutex_t;

#define RTDM_MUTEX_DESTROY  2

static inline int rtdm_mutex_init(rtdm_mutex_t *mutex)
{
	rt_mutex_init(&mutex->lock);
	clear_bit(RTDM_MUTEX_DESTROY, &mutex->state);
	smp_mb();
	return 0;
}

static inline int rtdm_mutex_lock(rtdm_mutex_t *mutex)
{
	if (unlikely(test_bit(RTDM_MUTEX_DESTROY, &mutex->state)))
		return -EIDRM;
	rt_mutex_lock(&mutex->lock);
	if (unlikely(test_bit(RTDM_MUTEX_DESTROY, &mutex->state)))
		return -EIDRM;
	return 0;
}

static inline void rtdm_mutex_unlock(rtdm_mutex_t *mutex)
{
	rt_mutex_unlock(&mutex->lock);
}

static inline void rtdm_mutex_destroy(rtdm_mutex_t *mutex)
{
	set_bit(RTDM_MUTEX_DESTROY, &mutex->state);
	smp_mb();
	if (unlikely(rt_mutex_is_locked(&mutex->lock)))
		rt_mutex_unlock(&mutex->lock);
}

int rtdm_mutex_timedlock(rtdm_mutex_t *mutex,
			 nanosecs_rel_t timeout,
			 rtdm_toseq_t *toseq);

/*
 * Utility functions
 */

#define rtdm_printk(format, ...)    printk(format, ##__VA_ARGS__)

static inline void *rtdm_malloc(size_t size)
{
	return kmalloc(size, GFP_KERNEL);
}

static inline void rtdm_free(void *ptr)
{
	kfree(ptr);
}

#ifdef FIXME
int rtdm_mmap_to_user(rtdm_user_info_t * user_info,
		      void *src_addr, size_t len,
		      int prot, void **pptr,
		      struct vm_operations_struct *vm_ops,
		      void *vm_private_data);
int rtdm_iomap_to_user(rtdm_user_info_t * user_info,
		       unsigned long src_addr, size_t len,
		       int prot, void **pptr,
		       struct vm_operations_struct *vm_ops,
		       void *vm_private_data);
int rtdm_munmap(rtdm_user_info_t * user_info, void *ptr, size_t len);
#endif	/* FIXME */

static inline int rtdm_read_user_ok(rtdm_user_info_t * user_info,
				    const void __user * ptr, size_t size)
{
	return access_ok(VERIFY_READ, ptr, size);
}

static inline int rtdm_rw_user_ok(rtdm_user_info_t * user_info,
				  const void __user * ptr, size_t size)
{
	return access_ok(VERIFY_WRITE, ptr, size);
}

static inline int rtdm_copy_from_user(rtdm_user_info_t * user_info,
				      void *dst, const void __user * src,
				      size_t size)
{
	return __copy_from_user_inatomic(dst, src, size) ? -EFAULT : 0;
}

static inline int rtdm_safe_copy_from_user(rtdm_user_info_t * user_info,
					   void *dst, const void __user * src,
					   size_t size)
{
	return (!access_ok(VERIFY_READ, src, size) ||
		__copy_from_user_inatomic(dst, src, size));
}

static inline int rtdm_copy_to_user(rtdm_user_info_t * user_info,
				    void __user * dst, const void *src,
				    size_t size)
{
	return __copy_to_user_inatomic(dst, src, size) ? -EFAULT : 0;
}

static inline int rtdm_safe_copy_to_user(rtdm_user_info_t * user_info,
					 void __user * dst, const void *src,
					 size_t size)
{
	return (!access_ok(VERIFY_WRITE, dst, size) ||
		__copy_to_user_inatomic(dst, src, size));
}

#ifdef FIXME
static inline int rtdm_strncpy_from_user(rtdm_user_info_t * user_info,
					 char *dst,
					 const char __user * src, size_t count)
{
	if (unlikely(!access_ok(VERIFY_READ, src, 1)))
		return -EFAULT;
	return __xn_strncpy_from_user(user_info, dst, src, count);
}

#endif	/* FIXME */

#define rtdm_in_rt_context() (1)

#endif /* _RTDM_DRIVER_NATIVE_H */
