#!/usr/bin/env bash
set -euo pipefail

IMG="${SIMPLEFS_IMG:-/tmp/simplefs.img}"
SIZE="${SIMPLEFS_DISK_SIZE:-4M}"
LOOP_DEV="${SIMPLEFS_LOOP:-}"
STATE_FILE="${SIMPLEFS_LOOP_STATE:-.simplefs_loop}"

if [[ ! -f "$IMG" ]]; then
	truncate -s "$SIZE" "$IMG"
fi

if [[ -z "$LOOP_DEV" ]]; then
	LOOP_DEV=$(sudo -n losetup -f)
	sudo -n losetup "$LOOP_DEV" "$IMG"
fi

echo "$LOOP_DEV" > "$STATE_FILE"
echo "Loop device: $LOOP_DEV (image $IMG)"
echo "Example: sudo insmod kernel/simplefs.ko disk_name=$LOOP_DEV sb_offset1=0 sb_offset2=1 max_filename_len=32 max_file_sectors=1"
