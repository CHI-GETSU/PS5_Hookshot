#!/usr/bin/env bash
# The same game built for this PC (nx2/build.py --standalone, RX_CPM=.local/ps5-deps/cpm), run with no window and no sound
# from the PS5's autosave (work/pc/user, a copy of work/save-backup) out into the forest's opening shots: the reference a
# PS5 run is compared with (the flash counter, the draw statistics).
#   ps5/tools/pc-run.sh NAME [SECONDS] [KEY=VALUE ...]      -> work/pc/NAME.log, work/pc/NAME-shots
set -uo pipefail
cd "$(dirname "$0")/../../work/pc"
name=$1 seconds=${2:-200}
shift $(( $# >= 2 ? 2 : $# ))
rm -rf "$name-shots"; mkdir -p "$name-shots"; : > "$name-input.txt"
rm -rf "$name-stats"; mkdir -p "$name-stats"
game=../lift/sa/build
env SDL_AUDIODRIVER=dummy HOS_HEADLESS=1 RX_VK_SHOTS="$PWD/$name-shots" RX_VK_SHOT_EVERY=1 RX_VK_FLASH=1 RX_VK_FLASH_DUMPS=0 \
    HOS_INPUT_LIVE="$PWD/$name-input.txt" RX_VK_DRAW_STATS="$PWD/$name-stats" "$@" timeout -s KILL "$seconds" $game/hookshot ../lift/sa/image/image.bin \
    --romfs ../game/romfs --user "$PWD/user" > "$name.log" 2>&1 &
run=$!
sleep "${TITLE_AT:-40}"; echo "0:A,4:A,8:A,12:A,16:A,20:A,24:A,28:A" >> "$name-input.txt"
sleep "${HOUSE_AT:-70}"; echo "0:LDOWN/2500,4:A" >> "$name-input.txt"
# (STATS=<n>: that many draw-statistics captures once outside, 2.5 s apart, into work/pc/NAME-stats: RX_VK_DRAW_STATS)
if [ -n "${STATS:-}" ]; then
    sleep "${STATS_AT:-25}"
    for _ in $(seq "$STATS"); do : > "$name-stats/draw_stats.now"; sleep 2.5; done
fi
wait $run
echo "$name: flashes $(grep -ac 'flash at present' "$name.log") (first: $(grep -a 'flash at present' "$name.log" | head -1 | grep -o 'present [0-9]*' | head -1)); $(grep -a 'presents,' "$name.log" | tail -1 | sed 's/.*vk: //' | cut -c1-28)"
