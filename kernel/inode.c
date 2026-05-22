// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/blkdev.h>

#include "simplefs.h"

static struct inode *simplefs_get_inode(struct super_block *sb,
					const struct inode *dir,
					umode_t mode, int i_ino)
{
	struct inode *inode = new_inode(sb);
	struct timespec64 now;

	if (!inode)
		return NULL;

	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	inode->i_ino = i_ino;
	now = current_time(inode);
	inode_set_atime_to_ts(inode, now);
	inode_set_mtime_to_ts(inode, now);
	inode_set_ctime_to_ts(inode, now);
	return inode;
}

static struct inode *simplefs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	struct simplefs_sb_info *sbi = sb->s_fs_info;

	if (ino == SIMPLEFS_ROOT_INO) {
		inode = simplefs_get_inode(sb, NULL, S_IFDIR | 0755,
					   SIMPLEFS_ROOT_INO);
		if (!inode)
			return ERR_PTR(-ENOMEM);
		inode->i_op = &simplefs_dir_inode_ops;
		inode->i_fop = &simplefs_dir_ops;
		set_nlink(inode, 2);
		return inode;
	}

	if (ino < 2 || ino > sbi->file_count + 1)
		return ERR_PTR(-ENOENT);

	inode = simplefs_get_inode(sb, NULL, S_IFREG | 0644, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_op = &simplefs_inode_ops;
	inode->i_fop = &simplefs_file_ops;
	{
		struct simplefs_file_meta meta;

		if (!simplefs_read_file_meta(sb, ino - 2, &meta))
			inode->i_size = min_t(u64, le32_to_cpu(meta.data_size),
					      simplefs_file_data_size(
						      meta.sectors_used));
		else
			inode->i_size = 0;
	}
	return inode;
}

static struct dentry *simplefs_lookup(struct inode *dir, struct dentry *dentry,
				      unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	u32 i;

	for (i = 0; i < sbi->file_count; i++) {
		struct simplefs_file_meta meta;
		int err;

		err = simplefs_read_file_meta(sb, i, &meta);
		if (err)
			continue;
		if (!strncmp(meta.name, dentry->d_name.name,
			     dentry->d_name.len) &&
		    meta.name[dentry->d_name.len] == '\0') {
			struct inode *inode = simplefs_iget(sb, i + 2);

			if (IS_ERR(inode))
				return ERR_CAST(inode);
			d_add(dentry, inode);
			return NULL;
		}
	}
	d_add(dentry, NULL);
	return NULL;
}

static int simplefs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	u32 i;

	if (!dir_emit_dots(file, ctx))
		return 0;

	for (i = 0; i < sbi->file_count; i++) {
		struct simplefs_file_meta meta;
		int err;

		if (i + 2 < ctx->pos)
			continue;

		err = simplefs_read_file_meta(sb, i, &meta);
		if (err)
			continue;

		if (!dir_emit(ctx, meta.name, strnlen(meta.name,
						      sbi->max_filename_len),
			      i + 2, DT_REG))
			break;
		ctx->pos = i + 3;
	}
	return 0;
}

const struct inode_operations simplefs_dir_inode_ops = {
	.lookup = simplefs_lookup,
};

const struct file_operations simplefs_dir_ops = {
	.read = generic_read_dir,
	.iterate_shared = simplefs_readdir,
	.llseek = generic_file_llseek,
};

const struct inode_operations simplefs_inode_ops = {
	.getattr = simplefs_getattr,
	.setattr = simplefs_setattr,
};

int simplefs_getattr(struct mnt_idmap *idmap, const struct path *path,
		     struct kstat *stat, u32 request_mask,
		     unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);

	if (S_ISREG(inode->i_mode) && inode->i_ino >= 2) {
		struct simplefs_file_meta meta;

		if (!simplefs_read_file_meta(inode->i_sb, inode->i_ino - 2,
					     &meta))
			i_size_write(inode,
				     min_t(u64, le32_to_cpu(meta.data_size),
					   simplefs_file_data_size(
						   meta.sectors_used)));
	}

	generic_fillattr(idmap, request_mask, inode, stat);
	return 0;
}

int simplefs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		     struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;
	struct simplefs_sb_info *sbi = sb->s_fs_info;
	struct simplefs_file_meta meta;
	u32 index = inode->i_ino - 2;
	u64 capacity;
	int err;

	err = setattr_prepare(idmap, dentry, attr);
	if (err)
		return err;

	if (attr->ia_valid & ATTR_SIZE) {
		err = simplefs_read_file_meta(sb, index, &meta);
		if (err)
			return err;

		capacity = simplefs_file_data_size(meta.sectors_used);
		if (attr->ia_size > capacity)
			return -EFBIG;

		meta.data_size = cpu_to_le32(attr->ia_size);
		if (!attr->ia_size)
			meta.data_crc = cpu_to_le32(0);
		else
			meta.data_crc = cpu_to_le32(simplefs_data_crc(sb, index,
						      sbi->max_filename_len));

		err = simplefs_write_file_meta(sb, index, &meta);
		if (err)
			return err;
	}

	setattr_copy(idmap, inode, attr);
	return 0;
}

int simplefs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct simplefs_sb_info *sbi;
	struct inode *root;
	int err;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sbi->bdev = sb->s_bdev;
	sbi->sb_off1 = sb_offset1;
	sbi->sb_off2 = sb_offset2;
	sbi->max_filename_len = max_filename_len;
	sbi->max_file_sectors = max_file_sectors;
	sbi->sector_size = SIMPLEFS_SECTOR_SIZE;

	err = simplefs_read_super(sbi, &sbi->sb);
	if (err) {
		kfree(sbi);
		return err;
	}

	sbi->file_count = le32_to_cpu(sbi->sb.file_count);
	sbi->data_start = le32_to_cpu(sbi->sb.data_start_sector);

	sb->s_fs_info = sbi;
	sb->s_magic = SIMPLEFS_MAGIC;
	if (!sb_set_blocksize(sb, SIMPLEFS_BLOCK_SIZE)) {
		kfree(sbi);
		return -EINVAL;
	}
	sb->s_maxbytes = simplefs_file_data_size(sbi->max_file_sectors);
	sb->s_op = simplefs_sops_ptr;

	root = simplefs_iget(sb, SIMPLEFS_ROOT_INO);
	if (IS_ERR(root)) {
		kfree(sbi);
		return PTR_ERR(root);
	}

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		kfree(sbi);
		return -ENOMEM;
	}
	return 0;
}

void simplefs_kill_sb(struct super_block *sb)
{
	kill_block_super(sb);
}
