#!/usr/bin/env bash
# PS5: a run from launch towards Kokiri Forest: New Game once the title screen is up (15 screenshots in), then A every
# 4 s through the opening and the dialogue. Screenshots in work/runs/live, the log in work/runs/NAME.*.
#   ps5/tools/to-kokiri.sh NAME [SECONDS] [KEY=VALUE ...]   (more switches for hookshot_env.txt)
set -euo pipefail
cd "$(dirname "$0")/../.."
name=$1 seconds=${2:-600}
shots_at=${SHOTS_AT:-15}   # (with RX_VK_FLASH=1 the plain shots come every 300 presents: SHOTS_AT=6)
shift $(( $# >= 2 ? 2 : $# ))
extra=()
for kv in "$@"; do extra+=(--env "$kv"); done
python3 ps5/tools/live.py prepare
python3 ps5/tools/run.py "$name" --deploy --seconds "$seconds" --env RX_VK_SHOTS=/app0/shots --env RX_VK_SHOT_EVERY=120 \
    --env RX_VK_SHOT_W=640 --env RX_VK_SHOT_H=360 --env HOS_INPUT_LIVE=/app0/input.txt --env HOS_PS5_CPU=1 "${extra[@]}" \
    > "work/runs/$name.out" 2>&1 &
run=$!
shots() { python3 ps5/tools/live.py shot 2>/dev/null | grep -o '([0-9]* shots' | grep -o '[0-9]*' || echo 0; }
until [ "$(shots)" -ge "$shots_at" ] || ! kill -0 $run 2>/dev/null; do sleep 4; done
python3 ps5/tools/live.py press '0:A'
sleep 10
python3 ps5/tools/live.py press "$(python3 -c "print(','.join('%d:A' % (i*4) for i in range(${A_COUNT:-120})))")" > /dev/null
# (AFTER=<presses>: more input once the A presses are queued, timed from then: "45:LDOWN/2500,50:A" walks out of Link's
# house from the autosave there, into the forest's opening shots)
[ -n "${AFTER:-}" ] && python3 ps5/tools/live.py press "$AFTER" > /dev/null
echo "to-kokiri: A at the title, then A every 4 s, ${A_COUNT:-120} times (A_COUNT)"
wait $run
