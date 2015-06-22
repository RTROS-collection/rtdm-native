
/**
 * Real-Time Driver Model for native Linux-rt, device operation mux'ing
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

#include <linux/module.h>
#include "internal.h"

struct rtdm_dev_context *rtdm_context_get(int fd)
{
	struct rtdm_dev_context *context = NULL;

	return context;
}

static int _rtdm_open(struct inode *inode, struct file *file)
{
	struct rtdm_device *device;
	struct rtdm_dev_context *context;
	int ret = -ENOMEM;

	device = container_of(file->f_op, struct rtdm_device,
			      reserved.chrdev_ops);
	rtdm_reference_device(device);

	context = kmalloc(sizeof(struct rtdm_dev_context) +
			  device->context_size, GFP_KERNEL);

	if (likely(context)) {
		context->device = device;
		context->ops = &device->ops;
		context->context_flags = 0;
		context->fd = current->pid;
		atomic_set(&context->close_lock_count, 0);

		file->private_data = context;

		ret = device->open_nrt(context, current, file->f_flags);
		if (ret == -ENOSYS)
			ret = device->open_rt(context, current, file->f_flags);
	}
	return ret;
}

int rtdm_socket_lx(struct socket *sock, int protocol,
		   struct rtdm_device *device)
{
	struct rtdm_dev_context *context;
	struct sock *sk;
	int ret = -ENOMEM;

	rtdm_reference_device(device);
	sock->ops = &device->reserved.proto_ops;

	sk = sk_alloc(sock_net(sock->sk),
		      device->protocol_family,
		      GFP_KERNEL,
		      &device->reserved.proto);
	if (sk) {
		context = (struct rtdm_dev_context *)sk;

		context->device = device;
		context->ops = &device->ops;
		context->context_flags = 0;
		context->fd = current->pid;
		atomic_set(&context->close_lock_count, 0);

		sock_init_data(sock, sk);

		ret = device->socket_nrt(context, current, protocol);
		if (ret == -ENOSYS)
			ret = device->socket_rt(context, current, protocol);
	}
	return ret;
}

static int _rtdm_chrdev_release(struct inode *inode, struct file *file)
{
	struct rtdm_dev_context *context = file->private_data;
	int ret;

	if (!context)
		return -ENODEV;

	ret = context->ops->close_nrt(context, current);
	if (unlikely(ret == -ENOSYS))
		ret = context->ops->close_rt(context, current);
	else if (unlikely(ret < 0))
		return ret;

	rtdm_dereference_device(context->device);
	kfree(context);

	return ret;
}

static int _rtdm_sock_release(struct socket *sock)
{
	struct rtdm_dev_context *context =
		(struct rtdm_dev_context *)sock->sk;
	int ret;

	ret = context->ops->close_nrt(context, current);
	if (unlikely(ret < 0))
		return ret;

	rtdm_dereference_device(context->device);
	sk_free(sock->sk);
	sock->sk = NULL;

	return ret;
}

#define MAJOR_CHRDEV_FUNCTION_WRAPPER(operation, args...) \
{ \
	struct rtdm_dev_context *context = file->private_data; \
	struct rtdm_operations  *ops; \
	int                     ret; \
	ops = context->ops; \
	ret = ops->operation##_rt(context, current, args); \
	if (ret == -ENOSYS) \
		ret = ops->operation##_nrt(context, current, args); \
	return ret; \
}

#define MAJOR_SOCKET_FUNCTION_WRAPPER(operation, args...) \
{ \
	struct rtdm_dev_context *context = (struct rtdm_dev_context *)sock->sk; \
	struct rtdm_operations  *ops; \
	int                     ret; \
	ops = context->ops; \
	ret = ops->operation##_rt(context, current, args); \
	if (ret == -ENOSYS) \
		ret = ops->operation##_nrt(context, current, args); \
	return ret; \
}

long _rtdm_chrdev_ioctl(struct file *file, unsigned int request,
			unsigned long arg)
{
	MAJOR_CHRDEV_FUNCTION_WRAPPER(ioctl, request, (void *)arg);
}

ssize_t _rtdm_read(struct file *file, char *buf, size_t nbyte, loff_t * offs)
{
	MAJOR_CHRDEV_FUNCTION_WRAPPER(read, buf, nbyte);
}

ssize_t _rtdm_write(struct file *file, const char *buf, size_t nbyte,
		    loff_t * offs)
{
	MAJOR_CHRDEV_FUNCTION_WRAPPER(write, buf, nbyte);
}

int _rtdm_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	MAJOR_SOCKET_FUNCTION_WRAPPER(ioctl, cmd, (void *)arg);
}

ssize_t _rtdm_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
			   struct msghdr *msg, size_t total_len, int flags)
{
	MAJOR_SOCKET_FUNCTION_WRAPPER(recvmsg, msg, flags);
}

ssize_t _rtdm_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
			   struct msghdr *msg, size_t total_len)
{
	MAJOR_SOCKET_FUNCTION_WRAPPER(sendmsg, msg, 0);
}

int _rtdm_sock_bind(struct socket *sock,
		    struct sockaddr *myaddr, int addrlen)
{
	struct _rtdm_setsockaddr_args args = {myaddr, addrlen};
        MAJOR_SOCKET_FUNCTION_WRAPPER(ioctl,  _RTIOC_BIND, (void *)&args);
}

int _rtdm_sock_setsockopt(struct socket *sock, int level,
			   int optname, char __user *optval, unsigned int optlen)
{
	struct _rtdm_setsockopt_args args = {level, optname, optval, optlen};
        MAJOR_SOCKET_FUNCTION_WRAPPER(ioctl,  _RTIOC_SETSOCKOPT, (void *)&args);
}

int _rtdm_sock_getsockopt(struct socket *sock, int level,
			  int optname, char __user *optval, int __user *optlen)
{
	struct _rtdm_getsockopt_args args = {level, optname, optval, optlen};
        MAJOR_SOCKET_FUNCTION_WRAPPER(ioctl,  _RTIOC_GETSOCKOPT, (void *)&args);
}

struct file_operations rtdm_chrdev_ops = {
	.open = _rtdm_open,
	.release = _rtdm_chrdev_release,
	.unlocked_ioctl = _rtdm_chrdev_ioctl,
	.read = _rtdm_read,
	.write = _rtdm_write,
};

struct proto_ops rtdm_proto_ops = {
	.bind = _rtdm_sock_bind,
	.setsockopt = _rtdm_sock_setsockopt,
	.getsockopt = _rtdm_sock_getsockopt,
	.release = _rtdm_sock_release,
	.ioctl = _rtdm_sock_ioctl,
	.sendmsg = _rtdm_sock_sendmsg,
	.recvmsg = _rtdm_sock_recvmsg,
};


EXPORT_SYMBOL_GPL(rtdm_context_get);
EXPORT_SYMBOL_GPL(_rtdm_open);
EXPORT_SYMBOL_GPL(rtdm_socket_lx);
