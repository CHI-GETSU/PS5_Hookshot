"""Game-free: a player's build starts from the tools the Linux AppImage carries. Runs the real tools-copy block of
scripts/linux/appimage.sh into a temporary folder, then nx2/build.py --standalone --dry-run from there, as the AppImage runs
it (HOOKSHOT_VK_PREBUILT: the Vulkan back end comes built), with stand-ins for the compiler, sysroot and FFmpeg it brings.
The dry run goes up to the compile, where the lifted game is first needed, and fails on any other file the compile would
read that is not in the tree. 2026-11-10: nx2/build.py wrote the menu's fonts from third_party/fonts at every start, the
AppImage does not carry that folder, and every Linux player's build stopped (#1567); CI had packaged the AppImage and never
started a build from it. The AppImage workflow does the same with the real image: scripts/linux/check_player_build.sh.

    python -I -B tests/test_appimage_player_build.py
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def package(tools):
    """appimage.sh's own copy of the tools into <tools> (its T). Without -e: the native helpers it copies (zbic.so) are
    built by appimage.sh first, and what the build needs of the tree is the dry run's to say"""
    script = (ROOT / "scripts/linux/appimage.sh").read_text(encoding="utf-8")
    start = script.index("# ---- the tools (the sources")
    end = script.index("# ---- what it builds with", start)
    subprocess.run(["bash", "-u", "-c", script[start:end]], cwd=ROOT, env=dict(os.environ, T=str(tools), VERSION="test"),
                   capture_output=True, text=True)


def dry_run(tools, lib, work):
    """nx2/build.py as nx2/make_game.py runs it in the AppImage (AppRun's variables), with nothing else of this PC's"""
    env = {"PATH": "/usr/bin:/bin", "HOME": str(work / "home"), "LANG": "C.UTF-8", "HOOKSHOT_TOOLS": str(tools),
           "EMSDK": str(lib / "emsdk"), "HOOKSHOT_SYSROOT": str(lib / "sysroot"), "HOOKSHOT_FFMPEG": str(lib / "ffmpeg"),
           "HOOKSHOT_VK_PREBUILT": str(lib / "hookshot_vk.so"), "OOT_WORK": str(work / "work"), "RX_RELEASE": "1",
           "A64_HOT_CHUNK": "1", "RX_MARCH": "x86-64-v3"}
    return subprocess.run([sys.executable, "-I", "-B", "-u", "-X", "utf8", "nx2/build.py", "--standalone", "--jobs", "1",
                           "--dry-run"], cwd=tools, env=env, capture_output=True, text=True, timeout=300)


@unittest.skipUnless(sys.platform.startswith("linux"), "the AppImage is made on Linux")
class PlayerBuildFromTheAppImage(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="hookshot-appimage-build-")
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.tools, self.lib = self.root / "AppDir/usr/share/hookshot", self.root / "AppDir/usr/lib/hookshot"
        self.tools.mkdir(parents=True)
        package(self.tools)
        # (stand-ins for what appimage.sh builds or brings: a compiler that is never run, the sysroot, FFmpeg, the back end)
        clang = self.lib / "emsdk/upstream/bin/clang"
        clang.parent.mkdir(parents=True)
        clang.write_text("#!/bin/sh\nexit 1\n")
        clang.chmod(0o755)
        for d in ("sysroot", "ffmpeg/include", "ffmpeg/lib"):
            (self.lib / d).mkdir(parents=True)
        (self.lib / "hookshot_vk.so").write_bytes(b"")
        (self.root / "home").mkdir()

    def test_starts_from_the_packaged_tree(self):
        r = dry_run(self.tools, self.lib, self.root)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("as built", r.stdout)   # (the prebuilt back end: nothing of gpu/vk compiled)
        self.assertRegex(r.stdout, r"dry run: [1-9]\d* files to compile besides the lift's, 0 missing")
        self.assertFalse(list(self.tools.rglob("__pycache__")))   # (the image is read-only on a player's PC)

    def test_a_file_the_image_lacks_fails(self):
        first = (self.tools / "third_party/opus/sources.txt").read_text().split()[0]
        (self.tools / "third_party/opus" / first).unlink()
        r = dry_run(self.tools, self.lib, self.root)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("missing: " + str(self.tools / "nx2" / ".." / "third_party" / "opus" / first), r.stdout)


class TheChecksAreWired(unittest.TestCase):
    def test_the_workflow_checks_the_image_it_made(self):
        flow = (ROOT / ".github/workflows/linux-appimage.yml").read_text(encoding="utf-8")
        self.assertLess(flow.index("run: scripts/linux/appimage.sh"), flow.index("run: scripts/linux/check_player_build.sh"))

    def test_apprun_sets_up_before_python(self):
        # (check_player_build.sh takes AppRun's environment from its lines before PY=)
        apprun = (ROOT / "builder/linux/AppRun").read_text(encoding="utf-8")
        head = apprun[:apprun.index("\nPY=")]
        self.assertRegex(head, r"\nexport HOOKSHOT_VK_PREBUILT=")
        self.assertNotRegex(head, r"\n\s*(exec|exit)\b")
        script = (ROOT / "scripts/linux/check_player_build.sh").read_text(encoding="utf-8")
        self.assertIn("sed '/^PY=/,$d'", script)
        self.assertIn("--dry-run", script)

    def test_the_test_sets_what_apprun_sets(self):
        apprun = (ROOT / "builder/linux/AppRun").read_text(encoding="utf-8")
        exported = set(re.findall(r"^export (HOOKSHOT_\w+|EMSDK)=", apprun, re.MULTILINE))
        source = Path(__file__).read_text(encoding="utf-8")
        used = set(re.findall(r'"(HOOKSHOT_\w+|EMSDK)":', source))
        self.assertEqual(exported - used - {"HOOKSHOT_SDL2", "HOOKSHOT_BUILDER_EXE"}, set())   # (those two: the game's, after the build)


if __name__ == "__main__":
    unittest.main()
