/**
 * Real-Time Driver Model for Linux (PREEMPT_RT), device management
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

/*!
 * @addtogroup driverapi
 * @{
 */

#include <linux/module.h>
#include "internal.h"

#define SET_DEFAULT_OP(device, operation) do { \
	(device).operation##_rt  = (void *)rtdm_no_support; \
	(device).operation##_nrt = (void *)rtdm_no_support; \
} while (0)

#define SET_DEFAULT_OP_IF_NULL(device, operation) do { \
	if (!(device).operation##_rt) \
		(device).operation##_rt = (void *)rtdm_no_support; \
	if (!(device).operation##_nrt) \
		(device).operation##_nrt = (void *)rtdm_no_support; \
} while (0)

#define ANY_HANDLER(device, operation) \
	((device).operation##_rt || (device).operation##_nrt)

DEFINE_MUTEX(rtdm_dev_lock);

int rtdm_no_support(void)
{
	return -ENOSYS;
}

int rtdm_dev_register(struct rtdm_device *device)
{
	int ret = 0;

	/* Sanity check: structure version */
	RTDM_ASSERT((device->struct_version == RTDM_DEVICE_STRUCT_VER),
		    printk("RTDM: invalid rtdm_device version (%d, required %d)\n",
			   device->struct_version, RTDM_DEVICE_STRUCT_VER);
		    return -EINVAL;);

	/* Sanity check: proc_name specified? */
	RTDM_ASSERT((device->proc_name),
		    printk("RTDM: no /proc entry name specified\n");
		    return -EINVAL;);

	switch (device->device_flags & RTDM_DEVICE_TYPE_MASK) {
	case RTDM_NAMED_DEVICE:
		/* Sanity check: any open handler? */
		RTDM_ASSERT(ANY_HANDLER(*device, open),
			    printk("RTDM: no open handler\n");
			    return -EINVAL;);
		SET_DEFAULT_OP_IF_NULL(*device, open);
		SET_DEFAULT_OP(*device, socket);
		break;

	case RTDM_PROTOCOL_DEVICE:
		/* Sanity check: any socket handler? */
		RTDM_ASSERT(ANY_HANDLER(*device, socket),
			    printk("RTDM: no socket handler\n");
			    return -EINVAL;);
		SET_DEFAULT_OP_IF_NULL(*device, socket);
		SET_DEFAULT_OP(*device, open);
		break;

	default:
		return -EINVAL;
	}

	/* Sanity check: non-RT close handler?
	 * (Always required for forced cleanup) */
	RTDM_ASSERT((device->ops.close_nrt),
		    printk("RTDM: no non-RT close handler\n");
		    return -EINVAL;);

	SET_DEFAULT_OP_IF_NULL(device->ops, close);
	SET_DEFAULT_OP_IF_NULL(device->ops, ioctl);
	SET_DEFAULT_OP_IF_NULL(device->ops, read);
	SET_DEFAULT_OP_IF_NULL(device->ops, write);
	SET_DEFAULT_OP_IF_NULL(device->ops, recvmsg);
	SET_DEFAULT_OP_IF_NULL(device->ops, sendmsg);

	atomic_set(&device->reserved.refcount, 0);

	mutex_lock(&rtdm_dev_lock);

	if ((device->device_flags & RTDM_DEVICE_TYPE_MASK) == RTDM_NAMED_DEVICE) {
		memcpy(&device->reserved.chrdev_ops, &rtdm_chrdev_ops,
		       sizeof(struct file_operations));
		ret = register_chrdev(0, device->device_name,
				      &device->reserved.chrdev_ops);
		device->reserved.chrdev_major = ret;
	} else {

		memset(&device->reserved.proto, 0, sizeof(device->reserved.proto));
		strcpy(device->reserved.proto.name, device->proc_name);
		device->reserved.proto.obj_size =
			sizeof(struct rtdm_dev_context) + device->context_size;
		device->reserved.proto.owner = THIS_MODULE;
		ret = proto_register(&device->reserved.proto, 0);
		printk("proto_register() returned %d\n", ret);
		if (ret)
			goto out;
		device->reserved.proto_ops = rtdm_proto_ops;

		memset(&device->reserved.proto_family, 0,
		       sizeof(device->reserved.proto_family));
		device->reserved.proto_family.family = device->protocol_family;
		device->reserved.proto_family.create = device->socket_lx;
		ret = sock_register(&device->reserved.proto_family);
		printk("sock_register() returned %d, proto=%d\n", ret,
		       device->reserved.proto_family.family);
	}

out:
        mutex_unlock(&rtdm_dev_lock);
	return ret;
}

EXPORT_SYMBOL_GPL(rtdm_dev_register);

int rtdm_dev_unregister(struct rtdm_device *device, unsigned int poll_delay)
{
	if (!device ||
	    (device->device_flags & RTDM_DEVICE_TYPE_MASK) == 0)
		return -ENODEV;

	mutex_lock(&rtdm_dev_lock);

	while (atomic_read(&device->reserved.refcount) > 0) {
		mutex_unlock(&rtdm_dev_lock);
		if (!poll_delay)
			return -EAGAIN;
		if (unlikely(CONFIG_XENO_OPT_DEBUG_RTDM > 0))
			printk("device %s still in use - waiting for release...\n",
			       device->proc_name);
		msleep(poll_delay);
		mutex_lock(&rtdm_dev_lock);
	}
	if ((device->device_flags & RTDM_DEVICE_TYPE_MASK) == RTDM_NAMED_DEVICE) {
		unregister_chrdev(device->reserved.chrdev_major,
				  device->device_name);
	} else  {
		sock_unregister(device->protocol_family);
		proto_unregister(&device->reserved.proto);
	}
	mutex_unlock(&rtdm_dev_lock);

	return 0;
}

EXPORT_SYMBOL_GPL(rtdm_dev_unregister);
