/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SIMPLEFS_H
#define _SIMPLEFS_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>

#define SIMPLEFS_MAGIC		0x53465331 /* "SFS1" */
#define SIMPLEFS_VERSION	1
#define SIMPLEFS_ROOT_INO	1

#define SIMPLEFS_META_SIZE	128

struct simplefs_super_block {
	__le32 magic;
	__le32 version;
	__le32 sector_size;
	__le32 max_filename_len;
	__le32 max_file_sectors;
	__le64 total_sectors;
	__le32 data_start_sector;
	__le32 file_count;
	__le32 checksum;
};

struct simplefs_file_meta {
	char name[64];
	__u8 sectors_used;
	__u8 reserved[3];
	__le32 meta_crc;
	__le32 data_crc;
};

struct simplefs_sb_info {
	struct simplefs_super_block sb;
	struct block_device *bdev;
	sector_t sb_off1;
	sector_t sb_off2;
	u32 max_filename_len;
	u32 max_file_sectors;
	u32 sector_size;
	u32 file_count;
	u32 data_start;
};

extern char *disk_name;
extern int sb_offset1;
extern int sb_offset2;
extern int max_filename_len;
extern int max_file_sectors;

u32 simplefs_sb_checksum(const struct simplefs_super_block *sb);
int simplefs_read_super(struct simplefs_sb_info *sbi, struct simplefs_super_block *out);
int simplefs_write_super(struct simplefs_sb_info *sbi, const struct simplefs_super_block *sb);
int simplefs_format_disk(struct block_device *bdev);
int simplefs_read_file_meta(struct super_block *sb, u32 index,
			    struct simplefs_file_meta *meta);
int simplefs_write_file_meta(struct super_block *sb, u32 index,
			     const struct simplefs_file_meta *meta);
u32 simplefs_meta_crc(const struct simplefs_file_meta *meta, u32 name_len);
u32 simplefs_data_crc(struct super_block *sb, u32 index, u32 name_len);

extern const struct super_operations *simplefs_sops_ptr;
extern const struct inode_operations simplefs_inode_ops;
extern const struct file_operations simplefs_file_ops;
extern const struct inode_operations simplefs_dir_inode_ops;
extern const struct file_operations simplefs_dir_ops;

int simplefs_fill_super(struct super_block *sb, void *data, int silent);
void simplefs_kill_sb(struct super_block *sb);

long simplefs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif /* _SIMPLEFS_H */
