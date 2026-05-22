#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

MOUNTPOINT="${SIMPLEFS_MOUNT:-/mnt}"
LOOP_STATE="${SIMPLEFS_LOOP_STATE:-.simplefs_loop}"
LOOP="${SIMPLEFS_LOOP:-}"
MAX_FILE_SECTORS="${SIMPLEFS_MAX_FILE_SECTORS:-1}"

if [[ -z "$LOOP" && -f "$LOOP_STATE" ]]; then
	LOOP=$(<"$LOOP_STATE")
fi

if [[ -z "$LOOP" ]]; then
	echo "Loop device is not set. Run scripts/setup_loop.sh first or set SIMPLEFS_LOOP=/dev/loopX." >&2
	exit 1
fi

cleanup()
{
	sudo -n umount "$MOUNTPOINT" 2>/dev/null || true
	sudo -n rmmod simplefs 2>/dev/null || true
}

trap cleanup EXIT

cleanup
make

sudo -n insmod kernel/simplefs.ko disk_name="$LOOP" sb_offset1=0 sb_offset2=1 \
	max_filename_len=32 max_file_sectors="$MAX_FILE_SECTORS"
sudo -n mkdir -p "$MOUNTPOINT"
sudo -n mount -t simplefs "$LOOP" "$MOUNTPOINT"

echo "Mounted $LOOP at $MOUNTPOINT with max_file_sectors=$MAX_FILE_SECTORS"
echo "File count: $(find "$MOUNTPOINT" -maxdepth 1 -type f | wc -l)"
ls -la "$MOUNTPOINT" | sed -n '1,20p'

./userspace/simplefs_test "$MOUNTPOINT"
./userspace/simplefs_test "$MOUNTPOINT" meta | sed -n '1,10p'
./userspace/simplefs_test "$MOUNTPOINT" map file0000
./userspace/simplefs_test "$MOUNTPOINT" zero
./userspace/simplefs_test "$MOUNTPOINT" wipe

sudo -n umount "$MOUNTPOINT"
sudo -n mount -t simplefs "$LOOP" "$MOUNTPOINT"
echo "File count after wipe: $(find "$MOUNTPOINT" -maxdepth 1 -type f | wc -l)"

echo "Demo OK"
