// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "simplefs.h"

static u32 inode_to_index(struct inode *inode)
{
	return inode->i_ino - 2;
}

static int simplefs_read_sector(struct super_block *sb, u32 index, u32 sec_off,
			      void *buf, unsigned len, unsigned offset_in_sec)
{
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	struct buffer_head *bh;
	sector_t sec;

	sec = sbi->data_start + index + sec_off;
	bh = sb_bread(sbi->bdev, sec);
	if (!bh)
		return -EIO;

	memcpy(buf, bh->b_data + offset_in_sec, len);
	brelse(bh);
	return 0;
}

static int simplefs_write_sector(struct super_block *sb, u32 index, u32 sec_off,
				 const void *buf, unsigned len,
				 unsigned offset_in_sec)
{
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	struct buffer_head *bh;
	sector_t sec;

	sec = sbi->data_start + index + sec_off;
	bh = sb_bread(sbi->bdev, sec);
	if (!bh)
		return -EIO;

	memcpy(bh->b_data + offset_in_sec, buf, len);
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
	return 0;
}

static ssize_t simplefs_read_file(struct file *file, char __user *buf,
				  size_t len, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	struct simplefs_file_meta meta;
	u32 index = inode_to_index(inode);
	size_t file_size, done = 0;
	int err;

	err = simplefs_read_file_meta(sb, index, &meta);
	if (err)
		return err;

	file_size = meta.sectors_used * sbi->sector_size;
	if (*ppos >= file_size)
		return 0;
	if (len > file_size - *ppos)
		len = file_size - *ppos;

	while (done < len) {
		loff_t pos = *ppos + done;
		u32 sec_off = pos / sbi->sector_size;
		unsigned off = pos % sbi->sector_size;
		unsigned chunk;
		void *kbuf;

		if (sec_off >= meta.sectors_used)
			break;

		if (sec_off == 0 && off < SIMPLEFS_META_SIZE)
			off = SIMPLEFS_META_SIZE;

		chunk = min_t(unsigned, len - done,
			      sbi->sector_size - off);

		kbuf = kmalloc(chunk, GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;

		err = simplefs_read_sector(sb, index, sec_off, kbuf, chunk, off);
		if (err) {
			kfree(kbuf);
			return err;
		}
		if (copy_to_user(buf + done, kbuf, chunk)) {
			kfree(kbuf);
			return -EFAULT;
		}
		kfree(kbuf);
		done += chunk;
	}
	*ppos += done;
	return done;
}

static ssize_t simplefs_write_file(struct file *file, const char __user *buf,
				   size_t len, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	struct simplefs_file_meta meta;
	u32 index = inode_to_index(inode);
	size_t max_size, done = 0;
	int err;

	err = simplefs_read_file_meta(sb, index, &meta);
	if (err)
		return err;

	max_size = meta.sectors_used * sbi->sector_size;
	if (*ppos + len > max_size)
		len = max_size - *ppos;
	if ((ssize_t)len <= 0)
		return 0;

	while (done < len) {
		loff_t pos = *ppos + done;
		u32 sec_off = pos / sbi->sector_size;
		unsigned off = pos % sbi->sector_size;
		unsigned chunk;
		void *kbuf;

		if (sec_off == 0 && off < SIMPLEFS_META_SIZE)
			off = SIMPLEFS_META_SIZE;

		chunk = min_t(unsigned, len - done,
			      sbi->sector_size - off);

		kbuf = kmalloc(chunk, GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;

		if (copy_from_user(kbuf, buf + done, chunk)) {
			kfree(kbuf);
			return -EFAULT;
		}
		err = simplefs_write_sector(sb, index, sec_off, kbuf, chunk, off);
		kfree(kbuf);
		if (err)
			return err;
		done += chunk;
	}

	meta.data_crc = cpu_to_le32(simplefs_data_crc(sb, index,
						      sbi->max_filename_len));
	simplefs_write_file_meta(sb, index, &meta);

	*ppos += done;
	return done;
}

const struct file_operations simplefs_file_ops = {
	.read = simplefs_read_file,
	.write = simplefs_write_file,
	.llseek = generic_file_llseek,
	.unlocked_ioctl = simplefs_ioctl,
};
