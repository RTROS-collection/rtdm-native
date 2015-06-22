/**
 * Real-Time Driver Model for native Linux-rt, driver library
 *
 * Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 *
 * For i386 and x86-64 semaphore implementation.
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Portions Copyright 1999 Red Hat, Inc.
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

#include <asm/page.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/fs_struct.h>
#include <linux/fdtable.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <asm/atomic.h>

#include <rtdm/rtdm_driver.h>

/*
 * Interrupt management services
 */

static irqreturn_t _rtdm_irq_trampoline(int irq, void *cookie)
{
	rtdm_irq_t *irq_handle = cookie;

	if (irq_handle && irq_handle->handler) {
		return irq_handle->handler(irq_handle);
	}

	printk("_rtdm_irq_trampoline: spurious interrupt %d\n", irq);
	disable_irq(irq);
	return IRQ_NONE;
}

int rtdm_irq_request(rtdm_irq_t * irq_handle,
		     unsigned int irq_no,
		     rtdm_irq_handler_t handler,
		     unsigned long flags,
		     const char *device_name, void *arg)
{
	int ret;

	irq_handle->irq_no = irq_no;
	irq_handle->cookie = arg;
	irq_handle->handler = handler;

	ret =  request_irq(irq_no, _rtdm_irq_trampoline, flags,
			   device_name, irq_handle);
	return ret;
}

EXPORT_SYMBOL_GPL(rtdm_irq_request);

/*
 * Mutex services
 */

int rtdm_mutex_timedlock(rtdm_mutex_t *mutex,
			 nanosecs_rel_t timeout,
			 rtdm_toseq_t *toseq)
{
	int err = 0;

	if (unlikely(test_bit(RTDM_MUTEX_DESTROY, &mutex->state)))
		return -EIDRM;
	if (timeout > 0) {
		rtdm_toseq_t toseq_local;
		if (!toseq) {
			toseq = &toseq_local;
			rtdm_toseq_init(toseq, timeout);
		}
#ifdef RTMUTEX_DEADLOCK_ARG
		err = rt_mutex_timed_lock(&mutex->lock, toseq, 0);
#else
		err = rt_mutex_timed_lock(&mutex->lock, toseq);
#endif
	} else if (timeout < 0) {
		if (rt_mutex_trylock(&mutex->lock))
			return 0;
		else
			return -EWOULDBLOCK;
	} else {
#ifdef RTMUTEX_DEADLOCK_ARG
	    err = rt_mutex_lock_interruptible(&mutex->lock, 0);
#else
	    err = rt_mutex_lock_interruptible(&mutex->lock);
#endif
	}
	if (unlikely(test_bit(RTDM_MUTEX_DESTROY, &mutex->state)))
		err = -EIDRM;
	return err;
}

EXPORT_SYMBOL_GPL(rtdm_mutex_timedlock);

/*
 * Event services
 */

int _rtdm_event_wait(rtdm_event_t * event)
{
	DEFINE_WAIT(wait);
	int ret;

	prepare_to_wait(&event->wait, &wait, TASK_INTERRUPTIBLE);
	if (test_bit(RTDM_EVENT_DESTROY, &event->state))
		ret = -EIDRM;
	else if (signal_pending(current))
		ret = -EINTR;
	else if (test_and_clear_bit(RTDM_EVENT_PENDING, &event->state))
		ret = 0;
	else {
		schedule();
		/* Somebody woke us up, check again */
		if (test_bit(RTDM_EVENT_DESTROY, &event->state))
			ret = -EIDRM;
		else if (signal_pending(current))
			ret = -EINTR;
		else {
			clear_bit(RTDM_EVENT_PENDING, &event->state);
			ret = 0;
		}
	}
	finish_wait(&event->wait, &wait);
	return ret;
}

EXPORT_SYMBOL_GPL(_rtdm_event_wait);

int _rtdm_event_timedwait(rtdm_event_t *event,
			  nanosecs_rel_t timeout,
			  rtdm_toseq_t *toseq)
{
	if (timeout > 0) {
		DEFINE_WAIT(wait);
		rtdm_toseq_t toseq_local;
		int ret;

		prepare_to_wait(&event->wait, &wait, TASK_INTERRUPTIBLE);
		if (test_bit(RTDM_EVENT_DESTROY, &event->state))
			ret = -EIDRM;
		else if (signal_pending(current))
			ret = -EINTR;
		if (test_and_clear_bit(RTDM_EVENT_PENDING, &event->state))
			ret = 0;
		else {
			if (!toseq) {
				toseq = &toseq_local;
				rtdm_toseq_init(toseq, timeout);
			}
			hrtimer_start(&toseq->timer,
				      hrtimer_get_remaining (&toseq->timer), // XXX?
				      HRTIMER_MODE_ABS);
			schedule();
			/* Somebody woke us up, check again */
			if (test_bit(RTDM_EVENT_DESTROY, &event->state))
				ret = -EIDRM;
			else if (toseq->task == NULL)
				ret = -ETIMEDOUT;
			else if (signal_pending(current))
				ret = -EINTR;
			else {
				clear_bit(RTDM_EVENT_PENDING, &event->state);
				ret = 0;
			}
			hrtimer_cancel(&toseq->timer);
		}
		finish_wait(&event->wait, &wait);
		return ret;
	} else if (timeout == 0)
		return _rtdm_event_wait(event);
	else  /* timeout < 0 */
		return -EWOULDBLOCK;

	return 0;
}

EXPORT_SYMBOL_GPL(_rtdm_event_timedwait);

/*
 * Semaphore services
 */

int _rtdm_sem_down(rtdm_sem_t *sem)
{
	int ret = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	unsigned long flags;

	if (test_bit(RTDM_EVENT_DESTROY, &sem->state))
		return -EIDRM;

	might_sleep();

	tsk->state = TASK_INTERRUPTIBLE;
	spin_lock_irqsave(&sem->wait.lock, flags);
	__add_wait_queue_tail_exclusive(&sem->wait, &wait);

	sem->sleepers++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * With signals pending, this turns into
		 * the trylock failure case - we won't be
		 * sleeping, and we* can't get the lock as
		 * it has contention. Just correct the count
		 * and exit.
		 */
                if (test_bit(RTDM_EVENT_DESTROY, &sem->state)) {
			ret = -EIDRM;
			break;
		}
		else if (signal_pending(current)) {
			ret = -EINTR;
			sem->sleepers = 0;
			atomic_add(sleepers, &sem->count);
			break;
		}

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock in
		 * wait_queue_head. The "-1" is because we're
		 * still hoping to get the semaphore.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;/* us - see -1 above */
		spin_unlock_irqrestore(&sem->wait.lock, flags);

		schedule();

		spin_lock_irqsave(&sem->wait.lock, flags);
		tsk->state = TASK_INTERRUPTIBLE;
	}
	__remove_wait_queue(&sem->wait, &wait);
	wake_up_locked(&sem->wait);
	spin_unlock_irqrestore(&sem->wait.lock, flags);

	tsk->state = TASK_RUNNING;
	return ret;
}

EXPORT_SYMBOL_GPL(_rtdm_sem_down);

int _rtdm_sem_timeddown(rtdm_sem_t *sem, nanosecs_rel_t timeout,
			rtdm_toseq_t *toseq)
{
	int ret = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	unsigned long flags;
	rtdm_toseq_t toseq_local;
	int sleepers;

	if (test_bit(RTDM_EVENT_DESTROY, &sem->state))
		return -EIDRM;

	if (unlikely(timeout < 0)) {
		spin_lock_irqsave(&sem->wait.lock, flags);
		sleepers = sem->sleepers + 1;
		sem->sleepers = 0;

		/*
		 * Add "everybody else" and us into it. They aren't
		 * playing, because we own the spinlock in the
		 * wait_queue_head.
		 */
		if (!atomic_add_negative(sleepers, &sem->count))
			wake_up_locked(&sem->wait);

		spin_unlock_irqrestore(&sem->wait.lock, flags);
		return -EWOULDBLOCK;
	}

	might_sleep();

	if (likely(timeout)) {
		if (!toseq) {
			toseq = &toseq_local;
			rtdm_toseq_init(toseq, timeout);
		}
		hrtimer_start(&toseq->timer,
			      hrtimer_get_remaining (&toseq->timer), // XXX?
			      HRTIMER_MODE_ABS);
	}

	tsk->state = TASK_INTERRUPTIBLE;
	spin_lock_irqsave(&sem->wait.lock, flags);
	add_wait_queue_exclusive(&sem->wait, &wait);

	sem->sleepers++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * With signals pending, this turns into
		 * the trylock failure case - we won't be
		 * sleeping, and we* can't get the lock as
		 * it has contention. Just correct the count
		 * and exit.
		 */
                if (test_bit(RTDM_EVENT_DESTROY, &sem->state)) {
			ret = -EIDRM;
			break;
		}
		else if (signal_pending(current)) {
			ret = -EINTR;
			sem->sleepers = 0;
			atomic_add(sleepers, &sem->count);
			break;
		}
		else if (timeout && !toseq->task) {
			ret = -ETIMEDOUT;
			sem->sleepers = 0;
			atomic_add(sleepers, &sem->count);
			break;
		}

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock in
		 * wait_queue_head. The "-1" is because we're
		 * still hoping to get the semaphore.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;/* us - see -1 above */
		spin_unlock_irqrestore(&sem->wait.lock, flags);

		schedule();

		spin_lock_irqsave(&sem->wait.lock, flags);
		tsk->state = TASK_INTERRUPTIBLE;
	}
	remove_wait_queue(&sem->wait, &wait);
	wake_up_locked(&sem->wait);
	spin_unlock_irqrestore(&sem->wait.lock, flags);

	if (timeout)
		hrtimer_cancel(&toseq->timer);

	tsk->state = TASK_RUNNING;
	return ret;
}

EXPORT_SYMBOL_GPL(_rtdm_sem_timeddown);

/*
 * Task and timing services
 */


static void rtdm_task_exit_files(void)
{
        struct fs_struct *fs;
        struct task_struct *tsk = current;

        _exit_fs(tsk);           /* current->fs->users--; */
        fs = init_task.fs;
        tsk->fs = fs;
	spin_lock(&fs->lock);
	++fs->users;
	spin_unlock(&fs->lock);
        _exit_files(tsk);
        current->files = init_task.files;
        atomic_inc(&tsk->files->count);
}

static int rtdm_task(void* arg)
{
	int ret;
	rtdm_task_t *task = (rtdm_task_t *)arg;
	struct sched_param param = {.sched_priority = task->priority};
#if 1
	printk("rtdm_task pid %d\n", current->pid);
#endif
        rtdm_task_exit_files();

	/* By default we can run anywhere  */
	set_cpus_allowed(current, CPU_MASK_ALL);

	task->magic = RTDM_TASK_MAGIC;
	task->linux_task = current;
	current->flags |= PF_NOFREEZE;
	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	__set_current_state(TASK_INTERRUPTIBLE);
	complete(&task->start);
	schedule();
	ret = task->proc(task->arg);
#if 1
	printk("Exiting pid %d with %d\n", current->pid, ret);
#endif
	task->stopped = 1;

	return 0;
}

int rtdm_task_init(rtdm_task_t *task, const char *name,
		   rtdm_task_proc_t proc, void *arg,
		   int priority, nanosecs_rel_t period)
{
	struct task_struct *p;

	if (priority < RTDM_TASK_LOWEST_PRIORITY ||
	    priority > RTDM_TASK_HIGHEST_PRIORITY)
		return -EINVAL;

	task->proc = proc;
	task->arg = arg;
	task->priority = priority;

	init_completion(&task->start);
#if 1
	printk("rtdm_task init by %d\n", current->pid);
#endif
	p = kthread_run(rtdm_task, task, name);
	if (IS_ERR(p)) {
	        printk(KERN_WARNING "kthread_create(%s) failed with %ld\n", name, p->state);
		task->stopped = 1;
		return -EINVAL;
	} else {
		task->stopped = 0;
		wait_for_completion(&task->start);
		printk("kernel_thread(%s) succeeded, pid=%d\n", name, p->pid);
	}

	snprintf(task->linux_task->comm, sizeof(task->linux_task->comm),
		 "RTDM:%s", name);
	smp_mb();
	wake_up_process(p);

	return 0;
}

EXPORT_SYMBOL_GPL(rtdm_task_init);

void rtdm_task_set_priority(rtdm_task_t *task, int priority)
{
        if (rtdm_task_has_magic(task)) {
		struct sched_param param = {.sched_priority = priority};
		sched_setscheduler(task->linux_task, SCHED_FIFO, &param);
	} else
		printk("%s: not allowed on user threads\n", __FUNCTION__);
}

EXPORT_SYMBOL_GPL(rtdm_task_set_priority);

int _rtdm_task_sleep(struct hrtimer_sleeper *timeout)
{
	int ret;

	set_current_state(TASK_INTERRUPTIBLE);
	hrtimer_start(&timeout->timer,
		      hrtimer_get_remaining (&timeout->timer), // XXX?
		      HRTIMER_MODE_ABS);

	for (;;) {
	        if (kthread_should_stop())  {
			ret = -EINTR;
			break;
		}
		/* hrtimer expired */
		if (!timeout->task) {
			ret = 0;
			break;
		}
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);

	hrtimer_cancel(&timeout->timer);

	return ret;
}

EXPORT_SYMBOL_GPL(_rtdm_task_sleep);
