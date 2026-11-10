#!/usr/bin/env bash
# The PS5 fiber switch (host/posix/win32_posix.c, __PROSPERO__) built for the PC from that file's own assembly, and run.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
python3 - "$root" "$out/check.c" <<'PY'
import re, sys
src = open(sys.argv[1] + "/host/posix/win32_posix.c").read()
asm = re.search(r'(void hos_fiber_swap\(void \*\*save_sp, void \*load_sp\);.*?"\.size hos_fiber_entry, \.-hos_fiber_entry\\n"\);)', src, re.S).group(1)
open(sys.argv[2], "w").write(open(sys.argv[1] + "/ps5/tests/fiber_switch_check.c.in").read().replace("@ASM@", asm, 2))
PY
clang -O2 -pthread "$out/check.c" -o "$out/check"
"$out/check"
