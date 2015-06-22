/**
 * @file
 * Real-Time Driver Model for Linux (PREEMPT_RT)
 *
 * @note Copyright (C) 2005, 2006 Jan Kiszka <jan.kiszka@web.de>
 * @note Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
 *
 * RTDM is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*!
 * @ingroup rtdm
 * @defgroup profiles Device Profiles
 *
 * Device profiles define which operation handlers a driver of a certain class
 * has to implement, which name or protocol it has to register, which IOCTLs
 * it has to provide, and further details. Sub-classes can be defined in order
 * to extend a device profile with more hardware-specific functions.
 */

#include <linux/module.h>
#include <rtdm/rtdm_driver.h>

MODULE_DESCRIPTION("Real-Time Driver Model");
MODULE_AUTHOR("jan.kiszka@web.de");
MODULE_LICENSE("GPL");

exit_files_t _exit_files;
exit_fs_t _exit_fs;

int __init __rtdm_init(void)
{
	// kludge around unexported exit_fs/exit_files functions:
	_exit_fs = (exit_fs_t) kallsyms_lookup_name("exit_fs");
	_exit_files = (exit_files_t) kallsyms_lookup_name("exit_files");

	if (!_exit_fs || !_exit_files) {
	    printk(KERN_ERR "RTDM: could not resolve unexported symbols\n");
	    return -1;
	}

	printk(KERN_INFO "starting RTDM services.\n");
	return 0;
}

void __exit __rtdm_exit(void)
{
	printk(KERN_INFO "stopping RTDM services.\n");
}

module_init(__rtdm_init);
module_exit(__rtdm_exit);
