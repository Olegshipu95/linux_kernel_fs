// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <simplefs_ioctl.h>

static void
usage (const char *prog)
{
  fprintf (stderr,
           "Usage: %s <mountpoint> [command]\n"
           "  (no command)  — demo: write/read random u32 in each file\n"
           "  zero          — SIMPLEFS_IOC_ZERO_FILES\n"
           "  wipe          — SIMPLEFS_IOC_WIPE_FS\n"
           "  meta          — SIMPLEFS_IOC_GET_META\n"
           "  map <name>    — SIMPLEFS_IOC_GET_SECTOR_MAP\n",
           prog);
}

static int
open_any_file (const char *mnt, int *out_fd, char *path, size_t plen)
{
  DIR *d = opendir (mnt);
  struct dirent *e;

  if (!d)
    return -1;

  while ((e = readdir (d)))
    {
      if (e->d_name[0] == '.')
        continue;
      snprintf (path, plen, "%s/%s", mnt, e->d_name);
      *out_fd = open (path, O_RDWR);
      if (*out_fd >= 0)
        {
          closedir (d);
          return 0;
        }
    }
  closedir (d);
  return -1;
}

static int
cmd_zero (const char *mnt)
{
  char path[PATH_MAX];
  int fd, err = 0;

  if (open_any_file (mnt, &fd, path, sizeof (path)))
    {
      perror ("open file for ioctl");
      return 1;
    }
  if (ioctl (fd, SIMPLEFS_IOC_ZERO_FILES, 0))
    {
      perror ("ZERO_FILES");
      err = 1;
    }
  close (fd);
  return err;
}

static int
cmd_wipe (const char *mnt)
{
  char path[PATH_MAX];
  int fd, err = 0;

  if (open_any_file (mnt, &fd, path, sizeof (path)))
    {
      perror ("open file for ioctl");
      return 1;
    }
  if (ioctl (fd, SIMPLEFS_IOC_WIPE_FS, 0))
    {
      perror ("WIPE_FS");
      err = 1;
    }
  close (fd);
  return err;
}

static int
cmd_meta (const char *mnt)
{
  char path[PATH_MAX];
  int fd, i, err = 0;
  struct simplefs_meta_req req;
  struct simplefs_meta_entry *entries;

  if (open_any_file (mnt, &fd, path, sizeof (path)))
    {
      perror ("open file for ioctl");
      return 1;
    }

  memset (&req, 0, sizeof (req));
  if (ioctl (fd, SIMPLEFS_IOC_GET_META, &req))
    {
      perror ("GET_META");
      close (fd);
      return 1;
    }

  entries = calloc (req.count, sizeof (*entries));
  if (!entries)
    {
      perror ("calloc");
      close (fd);
      return 1;
    }

  req.capacity = req.count;
  req.entries = (uint64_t)(uintptr_t)entries;
  if (ioctl (fd, SIMPLEFS_IOC_GET_META, &req))
    {
      perror ("GET_META");
      free (entries);
      close (fd);
      return 1;
    }

  for (i = 0; i < (int)req.count && entries[i].name[0]; i++)
    {
      printf (
          "%s: sector=%u used=%u size=%u meta_crc=0x%08x data_crc=0x%08x\n",
          entries[i].name, entries[i].start_sector, entries[i].sectors_used,
          entries[i].data_size, entries[i].meta_crc, entries[i].data_crc);
    }
  free (entries);
  close (fd);
  return err;
}

static int
cmd_map (const char *mnt, const char *name)
{
  char path[PATH_MAX];
  int fd, i, err = 0;
  struct simplefs_sector_map_req req;

  if (open_any_file (mnt, &fd, path, sizeof (path)))
    {
      perror ("open file for ioctl");
      return 1;
    }
  memset (&req, 0, sizeof (req));
  strncpy (req.name, name, SIMPLEFS_MAX_NAME - 1);
  if (ioctl (fd, SIMPLEFS_IOC_GET_SECTOR_MAP, &req))
    {
      perror ("GET_SECTOR_MAP");
      close (fd);
      return 1;
    }
  printf ("%s: start=%u used=%u sectors:", req.name, req.start_sector,
          req.sectors_used);
  for (i = 0; i < req.sectors_used && i < SIMPLEFS_MAX_FILE_SECTORS; i++)
    printf (" %u", req.sectors[i]);
  printf ("\n");
  close (fd);
  return err;
}

static int
run_demo (const char *mnt)
{
  DIR *d = opendir (mnt);
  struct dirent *e;
  int failures = 0;

  if (!d)
    {
      perror ("opendir");
      return 1;
    }

  while ((e = readdir (d)))
    {
      char path[PATH_MAX];
      int fd;
      uint32_t w, r;
      ssize_t n;

      if (e->d_name[0] == '.')
        continue;

      snprintf (path, sizeof (path), "%s/%s", mnt, e->d_name);
      fd = open (path, O_RDWR);
      if (fd < 0)
        {
          perror (path);
          failures++;
          continue;
        }

      w = (uint32_t)random ();
      if (pwrite (fd, &w, sizeof (w), 0) != sizeof (w))
        {
          perror ("write");
          failures++;
          close (fd);
          continue;
        }

      r = 0;
      n = pread (fd, &r, sizeof (r), 0);
      if (n != sizeof (r) || r != w)
        {
          fprintf (stderr, "%s: mismatch wrote %u read %u\n", path, w, r);
          failures++;
        }
      else
        {
          printf ("%s: OK (%u)\n", path, r);
        }
      close (fd);
    }
  closedir (d);
  return failures ? 1 : 0;
}

int
main (int argc, char **argv)
{
  const char *mnt;
  const char *cmd = NULL;

  if (argc < 2)
    {
      usage (argv[0]);
      return 1;
    }

  mnt = argv[1];
  if (argc >= 3)
    cmd = argv[2];

  srandom ((unsigned)time (NULL));

  if (!cmd)
    return run_demo (mnt);

  if (!strcmp (cmd, "zero"))
    return cmd_zero (mnt);
  if (!strcmp (cmd, "wipe"))
    return cmd_wipe (mnt);
  if (!strcmp (cmd, "meta"))
    return cmd_meta (mnt);
  if (!strcmp (cmd, "map") && argc >= 4)
    return cmd_map (mnt, argv[3]);

  usage (argv[0]);
  return 1;
}
