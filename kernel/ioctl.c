// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/blkdev.h>

#include "simplefs.h"
#include "../include/uapi/simplefs_ioctl.h"

#define SIMPLEFS_MAP_MAX_SECTORS 64

static u32 inode_to_index(struct inode *inode)
{
	return inode->i_ino - 2;
}

static int find_file_index(struct super_block *sb, const char *name, u32 *out)
{
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	u32 i;

	for (i = 0; i < sbi->file_count; i++) {
		struct simplefs_file_meta meta;
		int err;

		err = simplefs_read_file_meta(sb, i, &meta);
		if (err)
			continue;
		if (!strncmp(meta.name, name, sbi->max_filename_len) &&
		    strnlen(meta.name, sbi->max_filename_len) == strlen(name)) {
			*out = i;
			return 0;
		}
	}
	return -ENOENT;
}

static int ioctl_zero_files(struct super_block *sb)
{
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	u32 i;

	for (i = 0; i < sbi->file_count; i++) {
		struct simplefs_file_meta meta;
		struct buffer_head *bh;
		sector_t sec;
		u32 j;

		if (simplefs_read_file_meta(sb, i, &meta))
			continue;

		for (j = 0; j < meta.sectors_used; j++) {
			sec = sbi->data_start + i + j;
			bh = sb_bread(sbi->bdev, sec);
			if (!bh)
				return -EIO;
			if (j == 0)
				memset(bh->b_data + SIMPLEFS_META_SIZE, 0,
				       sbi->sector_size - SIMPLEFS_META_SIZE);
			else
				memset(bh->b_data, 0, sbi->sector_size);
			mark_buffer_dirty(bh);
			sync_dirty_buffer(bh);
			brelse(bh);
		}
		meta.data_crc = cpu_to_le32(0);
		simplefs_write_file_meta(sb, i, &meta);
	}
	return 0;
}

static int ioctl_wipe_fs(struct super_block *sb)
{
	struct simplefs_sb_info *sbi = sb->s_fs_info;

	return simplefs_format_disk(sbi->bdev);
}

static int ioctl_get_meta(struct file *file, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	struct simplefs_meta_entry __user *uptr =
		(struct simplefs_meta_entry __user *)arg;
	struct simplefs_meta_entry entry;
	u32 i;

	for (i = 0; i < sbi->file_count; i++) {
		struct simplefs_file_meta meta;
		int err;

		err = simplefs_read_file_meta(sb, i, &meta);
		if (err)
			continue;

		memset(&entry, 0, sizeof(entry));
		strncpy(entry.name, meta.name, SIMPLEFS_MAX_NAME - 1);
		entry.start_sector = sbi->data_start + i;
		entry.sectors_used = meta.sectors_used;
		entry.meta_crc = le32_to_cpu(meta.meta_crc);
		entry.data_crc = le32_to_cpu(meta.data_crc);

		if (copy_to_user(&uptr[i], &entry, sizeof(entry)))
			return -EFAULT;
	}
	return 0;
}

static int ioctl_get_sector_map(struct file *file, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	struct simplefs_sector_map_req req;
	u32 index;
	u32 i;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	if (find_file_index(sb, req.name, &index))
		return -ENOENT;

	{
		struct simplefs_file_meta meta;

		if (simplefs_read_file_meta(sb, index, &meta))
			return -EIO;

		req.start_sector = sbi->data_start + index;
		req.sectors_used = meta.sectors_used;
		memset(req.sectors, 0, sizeof(req.sectors));
		for (i = 0; i < meta.sectors_used && i < SIMPLEFS_MAP_MAX_SECTORS;
		     i++)
			req.sectors[i] = req.start_sector + i;
	}

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

long simplefs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;

	switch (cmd) {
	case SIMPLEFS_IOC_ZERO_FILES:
		return ioctl_zero_files(sb);
	case SIMPLEFS_IOC_WIPE_FS:
		return ioctl_wipe_fs(sb);
	case SIMPLEFS_IOC_GET_META:
		return ioctl_get_meta(file, arg);
	case SIMPLEFS_IOC_GET_SECTOR_MAP:
		return ioctl_get_sector_map(file, arg);
	default:
		return -ENOTTY;
	}
}
