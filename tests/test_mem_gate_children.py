"""The memory gate reads what a compiler holds, not what its driver holds (nx2/build.py rss_mb): llvm-mingw's clang.exe is a small
driver that runs clang-23.exe as its child. Counting the driver alone counted every running compiler's whole need again on top of
the memory it already held, and Windows builds stayed at the floor's 4 compilers.

python -I -B tests/test_mem_gate_children.py
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "nx2"))
import build  # noqa: E402

HELD_MB = 300
CHILD = "import sys; held = b'x' * (%d << 20); print('ready', flush=True); sys.stdin.read()" % HELD_MB
DRIVER = "import subprocess, sys; sys.exit(subprocess.run([sys.executable, '-I', '-c', %r]).returncode)" % CHILD


def check(what, ok):
    if not ok:
        raise SystemExit("FAIL: " + what)


def main():
    check("no process counts for nothing", build.rss_mb(None) == 0)
    p = subprocess.Popen([sys.executable, "-I", "-c", DRIVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        check("the child started", p.stdout.readline().strip() == "ready")
        seen, end = 0, time.time() + 10
        while seen < HELD_MB and time.time() < end:
            seen = build.rss_mb(p)
            time.sleep(0.1)
        check("a driver's child counts (%d MB read, the child holds %d MB)" % (seen, HELD_MB), seen >= HELD_MB)
        check("a child is counted once (%d MB read)" % seen, seen < 2 * HELD_MB)
    finally:
        p.stdin.close()
        p.stdout.close()
        p.wait(30)
    check("a process that ended counts for nothing", build.rss_mb(p) == 0)
    print("ok")


if __name__ == "__main__":
    main()
