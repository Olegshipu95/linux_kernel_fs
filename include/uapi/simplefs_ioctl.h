/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SIMPLEFS_IOCTL_H
#define _SIMPLEFS_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SIMPLEFS_IOCTL_MAGIC	'S'

#define SIMPLEFS_IOC_ZERO_FILES		_IO(SIMPLEFS_IOCTL_MAGIC, 1)
#define SIMPLEFS_IOC_WIPE_FS		_IO(SIMPLEFS_IOCTL_MAGIC, 2)
#define SIMPLEFS_IOC_GET_META		_IOWR(SIMPLEFS_IOCTL_MAGIC, 3, struct simplefs_meta_req)
#define SIMPLEFS_IOC_GET_SECTOR_MAP	_IOWR(SIMPLEFS_IOCTL_MAGIC, 4, struct simplefs_sector_map_req)

#define SIMPLEFS_MAX_NAME	64
#define SIMPLEFS_MAX_FILE_SECTORS	64
#define SIMPLEFS_META_SIZE	128

struct simplefs_meta_entry {
	char name[SIMPLEFS_MAX_NAME];
	__u32 start_sector;
	__u8 sectors_used;
	__u8 pad[3];
	__u32 data_size;
	__u32 meta_crc;
	__u32 data_crc;
};

struct simplefs_meta_req {
	__u32 capacity;
	__u32 count;
	__u64 entries;
};

struct simplefs_sector_map_req {
	char name[SIMPLEFS_MAX_NAME];
	__u32 start_sector;
	__u8 sectors_used;
	__u8 pad[3];
	__u32 sectors[SIMPLEFS_MAX_FILE_SECTORS];
};

#endif /* _SIMPLEFS_IOCTL_H */
