// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/blkdev.h>

#include "simplefs.h"
#include "../include/uapi/simplefs_ioctl.h"

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
		unsigned int off_in_block;

		if (simplefs_read_file_meta(sb, i, &meta))
			continue;

		for (j = 0; j < meta.sectors_used; j++) {
			sec = simplefs_file_start(sbi, i) + j;
			bh = simplefs_bread_sb_sector(sb, sec);
			if (!bh)
				return -EIO;
			off_in_block = simplefs_sector_block_offset(sec);
			if (j == 0)
				memset(bh->b_data + off_in_block +
					       SIMPLEFS_META_SIZE,
				       0,
				       sbi->sector_size - SIMPLEFS_META_SIZE);
			else
				memset(bh->b_data + off_in_block, 0,
				       sbi->sector_size);
			mark_buffer_dirty(bh);
			sync_dirty_buffer(bh);
			brelse(bh);
		}
		meta.data_size = cpu_to_le32(0);
		meta.data_crc = cpu_to_le32(0);
		if (simplefs_write_file_meta(sb, i, &meta))
			return -EIO;
	}
	return 0;
}

static int ioctl_wipe_fs(struct super_block *sb)
{
	struct simplefs_sb_info *sbi = sb->s_fs_info;

	int err;

	err = simplefs_format_disk(sbi->bdev);
	if (err)
		return err;
	err = simplefs_read_super(sbi, &sbi->sb);
	if (err)
		return err;
	sbi->file_count = le32_to_cpu(sbi->sb.file_count);
	sbi->data_start = le32_to_cpu(sbi->sb.data_start_sector);
	return 0;
}

static int ioctl_get_meta(struct file *file, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	struct simplefs_meta_req req;
	struct simplefs_meta_entry __user *uptr;
	struct simplefs_meta_entry entry;
	u32 i, copied = 0;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	req.count = sbi->file_count;
	if (!req.capacity || !req.entries)
		return copy_to_user((void __user *)arg, &req, sizeof(req)) ?
		       -EFAULT : 0;

	uptr = (struct simplefs_meta_entry __user *)(unsigned long)req.entries;

	for (i = 0; i < sbi->file_count; i++) {
		struct simplefs_file_meta meta;
		int err;

		if (copied >= req.capacity)
			break;

		err = simplefs_read_file_meta(sb, i, &meta);
		if (err) {
			pr_warn("simplefs: skipping corrupt metadata at index %u\n",
				i);
			continue;
		}

		memset(&entry, 0, sizeof(entry));
		strncpy(entry.name, meta.name, SIMPLEFS_MAX_NAME - 1);
		entry.start_sector = simplefs_file_start(sbi, i);
		entry.sectors_used = meta.sectors_used;
		entry.data_size = le32_to_cpu(meta.data_size);
		entry.meta_crc = le32_to_cpu(meta.meta_crc);
		entry.data_crc = le32_to_cpu(meta.data_crc);

		if (copy_to_user(&uptr[copied], &entry, sizeof(entry)))
			return -EFAULT;
		copied++;
	}

	if (copy_to_user((void __user *)arg, &req, sizeof(req)))
		return -EFAULT;
	return req.capacity < sbi->file_count ? -ENOSPC : 0;
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

		req.start_sector = simplefs_file_start(sbi, index);
		req.sectors_used = meta.sectors_used;
		memset(req.sectors, 0, sizeof(req.sectors));
		for (i = 0;
		     i < meta.sectors_used && i < SIMPLEFS_MAX_FILE_SECTORS;
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
