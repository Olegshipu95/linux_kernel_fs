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
	unsigned int off_in_block;

	sec = simplefs_file_start(sbi, index) + sec_off;
	bh = simplefs_bread_sb_sector(sb, sec);
	if (!bh)
		return -EIO;

	off_in_block = simplefs_sector_block_offset(sec);
	memcpy(buf, bh->b_data + off_in_block + offset_in_sec, len);
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
	unsigned int off_in_block;

	sec = simplefs_file_start(sbi, index) + sec_off;
	bh = simplefs_bread_sb_sector(sb, sec);
	if (!bh)
		return -EIO;

	off_in_block = simplefs_sector_block_offset(sec);
	memcpy(bh->b_data + off_in_block + offset_in_sec, buf, len);
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

	file_size = min_t(u64, le32_to_cpu(meta.data_size),
			  simplefs_file_data_size(meta.sectors_used));
	if (*ppos < 0)
		return -EINVAL;
	if ((u64)*ppos >= file_size)
		return 0;
	if (len > file_size - (u64)*ppos)
		len = file_size - (u64)*ppos;

	while (done < len) {
		u64 phys_pos = *ppos + done + SIMPLEFS_META_SIZE;
		u32 sec_off = div_u64(phys_pos, sbi->sector_size);
		unsigned off = do_div(phys_pos, sbi->sector_size);
		unsigned chunk;
		void *kbuf;

		if (sec_off >= meta.sectors_used)
			break;

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
	u32 data_size;
	int err;

	err = simplefs_read_file_meta(sb, index, &meta);
	if (err)
		return err;

	max_size = simplefs_file_data_size(meta.sectors_used);
	if (*ppos < 0)
		return -EINVAL;
	if (file->f_flags & O_APPEND)
		*ppos = min_t(u32, le32_to_cpu(meta.data_size), max_size);
	if ((u64)*ppos >= max_size)
		return 0;
	if (len > max_size - (u64)*ppos)
		len = max_size - (u64)*ppos;
	if ((ssize_t)len <= 0)
		return 0;

	while (done < len) {
		u64 phys_pos = *ppos + done + SIMPLEFS_META_SIZE;
		u32 sec_off = div_u64(phys_pos, sbi->sector_size);
		unsigned off = do_div(phys_pos, sbi->sector_size);
		unsigned chunk;
		void *kbuf;

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

	data_size = max_t(u32, le32_to_cpu(meta.data_size), *ppos + done);
	meta.data_size = cpu_to_le32(data_size);
	meta.data_crc = cpu_to_le32(simplefs_data_crc(sb, index,
						      sbi->max_filename_len));
	err = simplefs_write_file_meta(sb, index, &meta);
	if (err)
		return err;

	*ppos += done;
	i_size_write(inode, data_size);
	return done;
}

const struct file_operations simplefs_file_ops = {
	.read = simplefs_read_file,
	.write = simplefs_write_file,
	.llseek = generic_file_llseek,
	.unlocked_ioctl = simplefs_ioctl,
};
