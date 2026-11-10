#!/usr/bin/env bash
# PS5: a run from the autosave in Link's house out into the forest's opening shots, with the renderer's flash counter on, and
# what it found: the one-frame flashes (the foliage gone every other frame is ~1 a present there), the frame rate and the
# slow frames.
#   ps5/tools/flash-run.sh NAME SECONDS [KEY=VALUE ...]
set -uo pipefail
cd "$(dirname "$0")/../.."
name=$1 seconds=$2
shift 2
python3 ps5/tools/save.py restore > /dev/null 2>&1 || true   # (the autosave in Link's house: work/save-backup)
A_COUNT=8 AFTER="45:LDOWN/2500,50:A" SHOTS_AT=6 ps5/tools/to-kokiri.sh "$name" "$seconds" HOS_HANDHELD=1 RX_VK_TEX_MAX_DIM=1024 RX_VK_TEX_SKIP_MB=16 \
    RX_VK_ASTC_BC_ALL=1 RX_VK_ASTC_BC_MB=0 RX_VK_SHOT_EVERY=1 RX_VK_FLASH=1 RX_VK_FLASH_DUMPS=0 RX_VK_SLOW_LOG=300 "$@" > /dev/null 2>&1
log=work/runs/$name.log
echo "$name: $(tail -1 "work/runs/$name.out" | cut -c1-60)"
echo "  flashes: $(grep -ac 'flash at present' "$log") (first: $(grep -a 'flash at present' "$log" | head -1 | grep -o 'present [0-9]*' | head -1))"
echo "  last: $(grep -a 'presents,' "$log" | tail -1 | sed 's/.*vk: //' | cut -c1-28)"
echo "  fps by 600 presents: $(grep -a 'presents,' "$log" | sed 's/.*presents, \([0-9.]*\) fps.*/\1/' | tr '\n' ' ')"
echo "  slow frames (>300 ms): $(grep -ac 'slow frame at' "$log"); slowest per 600: $(grep -a 'the slowest' "$log" | sed 's/.*slowest \([0-9.]*\) ms.*/\1/' | tr '\n' ' ')"
echo "  memory: $(grep -a 'largest free piece' "work/runs/$name.out" | tail -1 | sed 's/.*guest pages/guest/' | cut -c1-140)"
