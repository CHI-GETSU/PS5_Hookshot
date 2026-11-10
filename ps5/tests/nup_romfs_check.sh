#!/usr/bin/env bash
# host/ps5/nup_romfs.c built for the PC (with AddressSanitizer) reads plain and zstd-packed romfs files of your .nup;
# each must equal what nx2/nup.py extracts. Needs the PC's libzstd and the lift's Python packages.
#   ps5/tests/nup_romfs_check.sh <your game.nup>
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
nup=$1 py=${PYTHON:-python3}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
clang -O1 -g -fsanitize=address,undefined -I "$root/host/ps5" "$root/host/ps5/nup_romfs.c" "$root/ps5/tests/nup_romfs_check.c" \
    -lzstd -lpthread -o "$out/check"
names=$("$py" -I - "$root" "$nup" <<'PY'
import sys; sys.path.insert(0, sys.argv[1] + "/nx2")
import nup
n = nup.Nup(sys.argv[2]); t = nup.pick_title(n, None)
files = list(n.walk(t["romfs"]))
print(" ".join([rel for rel, r in files if r[5] == 1][:3] + [rel for rel, r in files if r[5] != 1][100:103]))
PY
)
# shellcheck disable=SC2086
OUTDIR=$out "$out/check" "$nup" $names
# shellcheck disable=SC2086
"$py" -I - "$root" "$nup" "$out" $names <<'PY'
import sys, io; sys.path.insert(0, sys.argv[1] + "/nx2")
import nup
n = nup.Nup(sys.argv[2]); t = nup.pick_title(n, None)
recs = {rel: r for rel, r in n.walk(t["romfs"])}
bad = 0
for i, rel in enumerate(sys.argv[4:], start=2):
    b = io.BytesIO(); n.copy_to(recs[rel], b, rel)
    bad += open("%s/out_%d" % (sys.argv[3], i), "rb").read() != b.getvalue()
print("nup_romfs: %d of %d files differ" % (bad, len(sys.argv) - 4))
sys.exit(1 if bad else 0)
PY
