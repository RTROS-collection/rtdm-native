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

#ifndef _RTDM_DRIVER_H
#define _RTDM_DRIVER_H

#ifndef __KERNEL__
#error This header is for kernel space usage only. \
       You are likely looking for rtdm/rtdm.h...
#endif				/* !__KERNEL__ */

#if defined(CONFIG_IPIPE)
#error Adeos/I-pipe not supported

#else
#ifndef CONFIG_HIGH_RES_TIMERS
#error High-resolution timers not enabled (CONFIG_HIGH_RES_TIMERS)
#endif
#include <rtdm/rtdm_driver_native.h>

#endif

typedef void (*exit_files_t)(struct task_struct *tsk);
typedef void (*exit_fs_t)(struct task_struct *tsk);

extern exit_files_t _exit_files;
extern exit_fs_t _exit_fs;

#endif /* _RTDM_DRIVER_H */


