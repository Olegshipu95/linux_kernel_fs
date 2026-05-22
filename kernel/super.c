#include <linux/blkdev.h>
#include <linux/crc32.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "simplefs.h"

u32
simplefs_sb_checksum (const struct simplefs_super_block *sb)
{
  return crc32_le (~0, (const u8 *)sb,
                   offsetof (struct simplefs_super_block, checksum));
}

static int
read_sb_at (struct block_device *bdev, sector_t off,
            struct simplefs_super_block *out)
{
  struct buffer_head *bh;
  const struct simplefs_super_block *disk;
  unsigned int off_in_block;

  bh = simplefs_bread_bdev_sector (bdev, off);
  if (!bh)
    return -EIO;

  off_in_block = simplefs_sector_block_offset (off);
  disk = (const struct simplefs_super_block *)(bh->b_data + off_in_block);
  memcpy (out, disk, sizeof (*out));
  brelse (bh);
  return 0;
}

static int
write_sb_at (struct block_device *bdev, sector_t off,
             const struct simplefs_super_block *sb)
{
  struct buffer_head *bh;
  unsigned int off_in_block;

  bh = simplefs_bread_bdev_sector (bdev, off);
  if (!bh)
    return -EIO;

  off_in_block = simplefs_sector_block_offset (off);
  memcpy (bh->b_data + off_in_block, sb, sizeof (*sb));
  mark_buffer_dirty (bh);
  sync_dirty_buffer (bh);
  brelse (bh);
  return 0;
}

static int
validate_sb (struct simplefs_super_block *sb)
{
  u32 crc;

  if (le32_to_cpu (sb->magic) != SIMPLEFS_MAGIC)
    return -EINVAL;
  if (le32_to_cpu (sb->version) != SIMPLEFS_VERSION)
    return -EINVAL;
  crc = simplefs_sb_checksum (sb);
  if (le32_to_cpu (sb->checksum) != crc)
    return -EINVAL;
  return 0;
}

int
simplefs_read_super (struct simplefs_sb_info *sbi,
                     struct simplefs_super_block *out)
{
  struct simplefs_super_block a, b;
  int va, vb;

  va = read_sb_at (sbi->bdev, sbi->sb_off1, &a);
  vb = read_sb_at (sbi->bdev, sbi->sb_off2, &b);

  if (!va && !validate_sb (&a))
    {
      *out = a;
      return 0;
    }
  if (!vb && !validate_sb (&b))
    {
      *out = b;
      return 0;
    }
  return -EIO;
}

int
simplefs_write_super (struct simplefs_sb_info *sbi,
                      const struct simplefs_super_block *sb)
{
  int err;

  err = write_sb_at (sbi->bdev, sbi->sb_off1, sb);
  if (err)
    return err;
  return write_sb_at (sbi->bdev, sbi->sb_off2, sb);
}

int
simplefs_format_disk (struct block_device *bdev)
{
  struct simplefs_super_block sb = { 0 };
  sector_t total, data_start, off1, off2;
  u32 file_count;
  int err;

  if (max_file_sectors <= 0 || max_file_sectors > SIMPLEFS_MAX_FILE_SECTORS
      || max_filename_len <= 0
      || max_filename_len > sizeof (((struct simplefs_file_meta *)0)->name)
      || sb_offset1 < 0 || sb_offset2 < 0 || sb_offset1 == sb_offset2)
    return -EINVAL;

  total = bdev_nr_sectors (bdev);
  off1 = sb_offset1;
  off2 = sb_offset2;
  data_start = (off1 > off2 ? off1 : off2) + 1;
  if (off1 >= total || off2 >= total || data_start >= total)
    return -EINVAL;

  file_count = div_u64 (total - data_start, max_file_sectors);
  if (!file_count)
    return -EINVAL;

  sb.magic = cpu_to_le32 (SIMPLEFS_MAGIC);
  sb.version = cpu_to_le32 (SIMPLEFS_VERSION);
  sb.sector_size = cpu_to_le32 (SIMPLEFS_SECTOR_SIZE);
  sb.max_filename_len = cpu_to_le32 (max_filename_len);
  sb.max_file_sectors = cpu_to_le32 (max_file_sectors);
  sb.total_sectors = cpu_to_le64 (total);
  sb.data_start_sector = cpu_to_le32 (data_start);
  sb.file_count = cpu_to_le32 (file_count);
  sb.checksum = cpu_to_le32 (simplefs_sb_checksum (&sb));

  err = write_sb_at (bdev, off1, &sb);
  if (err)
    return err;
  err = write_sb_at (bdev, off2, &sb);
  if (err)
    return err;

  {
    struct simplefs_sb_info sbi = {
      .bdev = bdev,
      .sb_off1 = off1,
      .sb_off2 = off2,
      .max_filename_len = max_filename_len,
      .max_file_sectors = max_file_sectors,
      .sector_size = SIMPLEFS_SECTOR_SIZE,
      .file_count = file_count,
      .data_start = data_start,
    };
    u32 i;

    memcpy (&sbi.sb, &sb, sizeof (sb));

    for (i = 0; i < file_count; i++)
      {
        struct simplefs_file_meta meta = { 0 };
        char fname[32];
        struct buffer_head *bh;
        sector_t sec = data_start + (sector_t)i * max_file_sectors;
        unsigned int off_in_block;

        snprintf (fname, sizeof (fname), "file%04u", i);
        strncpy (meta.name, fname, max_filename_len - 1);
        meta.sectors_used = max_file_sectors;
        meta.data_size = cpu_to_le32 (0);
        meta.meta_crc
            = cpu_to_le32 (simplefs_meta_crc (&meta, max_filename_len));
        meta.data_crc = cpu_to_le32 (0);

        bh = simplefs_bread_bdev_sector (bdev, sec);
        if (!bh)
          return -EIO;
        off_in_block = simplefs_sector_block_offset (sec);
        memset (bh->b_data + off_in_block, 0, SIMPLEFS_SECTOR_SIZE);
        memcpy (bh->b_data + off_in_block, &meta, sizeof (meta));
        mark_buffer_dirty (bh);
        sync_dirty_buffer (bh);
        brelse (bh);
      }
  }

  return 0;
}

u32
simplefs_meta_crc (const struct simplefs_file_meta *meta, u32 name_len)
{
  return crc32_le (~0, (const u8 *)meta,
                   offsetof (struct simplefs_file_meta, meta_crc));
}

static sector_t
file_sector (struct super_block *sb, u32 index)
{
  struct simplefs_sb_info *sbi = sb->s_fs_info;

  return simplefs_file_start (sbi, index);
}

int
simplefs_read_file_meta (struct super_block *sb, u32 index,
                         struct simplefs_file_meta *meta)
{
  struct simplefs_sb_info *sbi = sb->s_fs_info;
  struct buffer_head *bh;
  unsigned int off_in_block;

  if (index >= sbi->file_count)
    return -EINVAL;

  bh = simplefs_bread_sb_sector (sb, file_sector (sb, index));
  if (!bh)
    return -EIO;

  off_in_block = simplefs_sector_block_offset (file_sector (sb, index));
  memcpy (meta, bh->b_data + off_in_block, sizeof (*meta));
  brelse (bh);

  if (le32_to_cpu (meta->meta_crc)
      != simplefs_meta_crc (meta, sbi->max_filename_len))
    return -EIO;

  return 0;
}

int
simplefs_write_file_meta (struct super_block *sb, u32 index,
                          const struct simplefs_file_meta *meta)
{
  struct simplefs_sb_info *sbi = sb->s_fs_info;
  struct buffer_head *bh;
  struct simplefs_file_meta m;
  unsigned int off_in_block;

  if (index >= sbi->file_count)
    return -EINVAL;

  m = *meta;
  m.meta_crc = cpu_to_le32 (simplefs_meta_crc (&m, sbi->max_filename_len));

  bh = simplefs_bread_sb_sector (sb, file_sector (sb, index));
  if (!bh)
    return -EIO;

  off_in_block = simplefs_sector_block_offset (file_sector (sb, index));
  memcpy (bh->b_data + off_in_block, &m, sizeof (m));
  mark_buffer_dirty (bh);
  sync_dirty_buffer (bh);
  brelse (bh);
  return 0;
}

u32
simplefs_data_crc (struct super_block *sb, u32 index, u32 name_len)
{
  struct simplefs_sb_info *sbi = sb->s_fs_info;
  struct simplefs_file_meta meta;
  struct buffer_head *bh;
  u32 crc = ~0;
  u32 i, used;
  sector_t sec;

  if (simplefs_read_file_meta (sb, index, &meta))
    return 0;

  used = meta.sectors_used;
  if (!used || used > sbi->max_file_sectors)
    used = 1;

  for (i = 0; i < used; i++)
    {
      sec = file_sector (sb, index) + i;
      bh = simplefs_bread_sb_sector (sb, sec);
      if (!bh)
        return 0;
      if (i == 0)
        crc = crc32_le (crc,
                        bh->b_data + simplefs_sector_block_offset (sec)
                            + SIMPLEFS_META_SIZE,
                        sbi->sector_size - SIMPLEFS_META_SIZE);
      else
        crc = crc32_le (crc, bh->b_data + simplefs_sector_block_offset (sec),
                        sbi->sector_size);
      brelse (bh);
    }
  return crc;
}
