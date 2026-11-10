#!/usr/bin/env bash
# PS5: a run as a player has it: no screenshots and no input file, the autosave loaded by timed presses. What the log says of it: the frame rate, the slow frames and what they were, the memory.
#   ps5/tools/play-run.sh NAME SECONDS [KEY=VALUE ...]
set -uo pipefail
cd "$(dirname "$0")/../.."
name=$1 seconds=$2
shift 2
extra=()
for kv in "$@"; do extra+=(--env "$kv"); done
# (the presses by the clock, in the game's own memory: a file of them, read 30 times a second from the title's folder, waits
# with every other file there whenever a large one is replaced, and the game with it. The title screen is up ~40 s in; the
# autosave is of Link outside his house)
# (SLOW=<ms>: the frames over that long are logged, RX_VK_SLOW_LOG. Not by default: it times every scope of the replay, which
# a player's run does not, and the clock reads and thread-local lookups of that were ~5% of the frame rate in Hyrule Castle Town)
python3 ps5/tools/run.py "$name" --deploy --seconds "$seconds" ${SLOW:+--env "RX_VK_SLOW_LOG=$SLOW"} --env HOS_PS5_CPU=1 \
    --env "HOS_INPUT_SCRIPT=${INPUT:-45:A,55:A,59:A,63:A,67:A,71:A,75:A,79:A}" "${extra[@]}" > "work/runs/$name.out" 2>&1
log=work/runs/$name.log
echo "$name: $(tail -1 "work/runs/$name.out" | cut -c1-60)"
echo "  last: $(grep -a 'presents,' "$log" | tail -1 | sed 's/.*vk: //' | cut -c1-28)"
echo "  fps by 120 presents: $(grep -a 'presents,' "$log" | sed 's/.*presents, \([0-9.]*\) fps.*/\1/' | tr '\n' ' ')"
echo "  frames over 100 ms: $(grep -ac 'slow frame at' "$log"); the slowest of each 600: $(grep -a 'the slowest' "$log" | sed 's/.*slowest \([0-9.]*\) ms.*/\1/' | tr '\n' ' ')"
echo "  shown: $(grep -a 'frame interpolation:' "$log" | tail -1 | sed 's/.*vk: //' | cut -c1-150)"
echo "  memory: $(grep -a 'largest free piece' "work/runs/$name.out" | tail -1 | sed 's/.*guest pages/guest/' | cut -c1-140)"
