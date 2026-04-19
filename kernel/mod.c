// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>

#include "simplefs.h"

char *disk_name;
module_param(disk_name, charp, 0444);
MODULE_PARM_DESC(disk_name, "Block device to format/use");

int sb_offset1 = 0;
module_param(sb_offset1, int, 0444);
MODULE_PARM_DESC(sb_offset1, "First superblock sector offset");

int sb_offset2 = 1;
module_param(sb_offset2, int, 0444);
MODULE_PARM_DESC(sb_offset2, "Second superblock sector offset");

int max_filename_len = 32;
module_param(max_filename_len, int, 0444);
MODULE_PARM_DESC(max_filename_len, "Maximum file name length");

int max_file_sectors = 1;
module_param(max_file_sectors, int, 0444);
MODULE_PARM_DESC(max_file_sectors, "Maximum file size in sectors (M)");

static void simplefs_put_super(struct super_block *sb)
{
	struct simplefs_sb_info *sbi = sb->s_fs_info;

	kfree(sbi);
}

static const struct super_operations simplefs_sops = {
	.put_super = simplefs_put_super,
};

const struct super_operations *simplefs_sops_ptr = &simplefs_sops;

static int simplefs_validate_params(void)
{
	if (!disk_name) {
		pr_err("simplefs: disk_name module parameter required\n");
		return -EINVAL;
	}
	if (sb_offset1 < 0 || sb_offset2 < 0 || sb_offset1 == sb_offset2) {
		pr_err("simplefs: invalid superblock offsets\n");
		return -EINVAL;
	}
	if (max_filename_len <= 0 ||
	    max_filename_len > sizeof(((struct simplefs_file_meta *)0)->name)) {
		pr_err("simplefs: invalid max_filename_len\n");
		return -EINVAL;
	}
	if (max_file_sectors <= 0 ||
	    max_file_sectors > SIMPLEFS_MAX_FILE_SECTORS) {
		pr_err("simplefs: invalid max_file_sectors\n");
		return -EINVAL;
	}
	return 0;
}

static struct dentry *simplefs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, simplefs_fill_super);
}

static struct file_system_type simplefs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "simplefs",
	.mount		= simplefs_mount,
	.kill_sb	= simplefs_kill_sb,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init simplefs_init(void)
{
	struct block_device *bdev;
	struct file *bdev_file;
	struct simplefs_sb_info sbi;
	struct simplefs_super_block sb;
	int err;

	err = simplefs_validate_params();
	if (err)
		return err;

	bdev_file = bdev_file_open_by_path(disk_name,
					    BLK_OPEN_READ | BLK_OPEN_WRITE,
					    NULL, NULL);
	if (IS_ERR(bdev_file)) {
		pr_err("simplefs: cannot open %s\n", disk_name);
		return PTR_ERR(bdev_file);
	}
	bdev = file_bdev(bdev_file);

	sbi.bdev = bdev;
	sbi.sb_off1 = sb_offset1;
	sbi.sb_off2 = sb_offset2;

	if (simplefs_read_super(&sbi, &sb)) {
		err = simplefs_format_disk(bdev);
		if (err) {
			bdev_fput(bdev_file);
			return err;
		}
		pr_info("simplefs: formatted %s\n", disk_name);
	}

	bdev_fput(bdev_file);

	err = register_filesystem(&simplefs_fs_type);
	if (err)
		return err;

	pr_info("simplefs: registered (sb@%d,%d)\n", sb_offset1, sb_offset2);
	return 0;
}

static void __exit simplefs_exit(void)
{
	unregister_filesystem(&simplefs_fs_type);
	pr_info("simplefs: unregistered\n");
}

module_init(simplefs_init);
module_exit(simplefs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SimpleFS homework");
MODULE_DESCRIPTION("Simple sector-based filesystem");
