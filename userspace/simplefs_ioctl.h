/* Userspace copy of ioctl definitions */
#ifndef SIMPLEFS_USERSPACE_IOCTL_H
#define SIMPLEFS_USERSPACE_IOCTL_H

#include <stdint.h>
#include <sys/ioctl.h>

#define SIMPLEFS_IOCTL_MAGIC	'S'

#define SIMPLEFS_IOC_ZERO_FILES		_IO(SIMPLEFS_IOCTL_MAGIC, 1)
#define SIMPLEFS_IOC_WIPE_FS		_IO(SIMPLEFS_IOCTL_MAGIC, 2)
#define SIMPLEFS_IOC_GET_META		_IOR(SIMPLEFS_IOCTL_MAGIC, 3, struct simplefs_meta_entry)
#define SIMPLEFS_IOC_GET_SECTOR_MAP	_IOWR(SIMPLEFS_IOCTL_MAGIC, 4, struct simplefs_sector_map_req)

#define SIMPLEFS_MAX_NAME	64
#define SIMPLEFS_META_SIZE	128

struct simplefs_meta_entry {
	char name[SIMPLEFS_MAX_NAME];
	uint32_t start_sector;
	uint8_t sectors_used;
	uint8_t pad[3];
	uint32_t meta_crc;
	uint32_t data_crc;
};

struct simplefs_sector_map_req {
	char name[SIMPLEFS_MAX_NAME];
	uint32_t start_sector;
	uint8_t sectors_used;
	uint8_t pad[3];
	uint32_t sectors[64];
};

#endif
