"""An exited compiler root must not inherit a stale or reused PID's memory counters."""
import os
from pathlib import Path
import subprocess
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "nx2"))
import build  # noqa: E402


def main():
    backend = "tree_ws" if os.name == "nt" else "tree_rss"
    p = subprocess.Popen([sys.executable, "-I", "-c",
                          "import sys; print('ready', flush=True); sys.stdin.read()"],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        assert p.stdout.readline().strip() == "ready"
        with patch.object(build, backend, return_value=512 << 20) as sample:
            assert build.rss_mb(p) == 512, "live root must keep counting its process tree"
            sample.assert_called_once_with(p.pid)
        with patch.object(build, backend, side_effect=OSError("counter unavailable")):
            assert build.rss_mb(p) == 0, "unavailable counters retain the unknown-memory fallback"
    finally:
        p.stdin.close()
        p.stdout.close()
        p.wait(30)
    # Keep the real Popen/OS handle alive. The backend models retained counters or
    # a PID now found in a different snapshot; the known exit must take precedence.
    with patch.object(build, backend, return_value=512 << 20) as sample:
        assert build.rss_mb(p) == 0, "known-exited compiler must not sample stale PID counters"
        assert build.rss_mb(None) == 0
        sample.assert_not_called()
    print("PASS: live-tree accounting, unavailable counters and known-exited roots")


if __name__ == "__main__":
    main()
