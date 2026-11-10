"""make_game.py's last step, hookshot --prepare-shaders, run against stub games: its stage and progress lines go on to the builder,
its own @@done and @@error don't (the builders read those as the build's end or failure), a failure or a silent hang leaves the work
to the first start (never a failed build), and the game runs in its own environment, as the builders' Play starts it.

python -I -B tests/test_prepare_at_build.py
"""
import ast
import contextlib
import importlib.util
import io
import json
import os
import re
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "nx2" / "make_game.py"
spec = importlib.util.spec_from_file_location("make_game_prepare", SOURCE)
make_game = importlib.util.module_from_spec(spec)
spec.loader.exec_module(make_game)
BUILDER = ROOT / "builder" / "linux" / "hookshot_builder.py"


def stub(code):
    """A stand-in for hookshot --prepare-shaders: authored Python, no game, no GPU"""
    return [sys.executable, "-I", "-c", code]


def prepared(cmd, **kwargs):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = make_game.prepare_shaders(cmd, **kwargs)
    return code, out.getvalue().splitlines()


def not_prepared(lines):
    return [l for l in lines if l.startswith(make_game.NOT_PREPARED)]


class PrepareShadersTests(unittest.TestCase):
    def test_progress_goes_on_and_the_games_done_does_not(self):
        code, lines = prepared(stub("print('hookshot v'); print('@@stage shaders Preparing shaders'); print('@@progress 0.500');"
                                    "print('@@done shaders')"))
        self.assertEqual(code, 0)
        self.assertEqual(lines, ["hookshot v", "@@stage shaders Preparing shaders", "@@progress 0.500", "done shaders"])

    def test_no_vulkan_leaves_them_to_the_first_start(self):
        code, lines = prepared(stub("import sys; print('@@error no usable Vulkan here'); sys.exit(3)"))
        self.assertEqual(code, 3)
        self.assertFalse(any(l.startswith("@@") for l in lines))
        self.assertIn("error no usable Vulkan here", lines)
        self.assertEqual(len(not_prepared(lines)), 1)
        self.assertIn("(no usable Vulkan here)", lines[-1])

    def test_a_crash_leaves_them_to_the_first_start(self):
        code, lines = prepared(stub("import sys; print('@@stage shaders Preparing shaders'); sys.exit(5)"))
        self.assertEqual(code, 5)
        self.assertIn("exit status 5", not_prepared(lines)[0])

    def test_a_silent_hang_is_stopped_and_not_a_failed_build(self):
        # (prints its stage, then nothing: as a driver deadlock or a crash dialog left open. silence is 15 minutes in a build)
        started = time.monotonic()
        code, lines = prepared(stub("import time; print('@@stage shaders Preparing shaders', flush=True); time.sleep(120)"),
                               silence=1)
        self.assertLess(time.monotonic() - started, 30)
        self.assertIsNone(code)
        self.assertEqual(lines[0], "@@stage shaders Preparing shaders")
        self.assertIn("printed nothing for 1 seconds and was stopped", not_prepared(lines)[0])

    def test_steady_output_is_not_silence(self):
        code, lines = prepared(stub("import time\nfor i in range(6):\n    print('@@progress %.3f' % (i / 6), flush=True); "
                                    "time.sleep(0.4)"), silence=1)
        self.assertEqual(code, 0)
        self.assertEqual(not_prepared(lines), [])

    def test_missing_game_is_not_a_failed_build(self):
        code, lines = prepared([str(ROOT / "no-such-folder" / "hookshot.exe"), "--prepare-shaders"])
        self.assertEqual(code, 1)
        self.assertEqual(len(not_prepared(lines)), 1)
        self.assertFalse(any(l.startswith("@@") for l in lines))

    def test_the_game_runs_in_its_folder_without_the_tools_environment(self):
        dropped = dict(HOOKSHOT_TOOLS="/builder/tools", HOOKSHOT_NO_PREPARE="0", PYTHONPATH="/builder/python",
                       PYTHONHOME="/builder", APPDIR="/appdir", APPIMAGE="/tmp/Hookshot.AppImage", ARGV0="Hookshot", OWD="/old",
                       LD_LIBRARY_PATH="/builder/lib")
        with tempfile.TemporaryDirectory(prefix="hookshot-prepare-") as folder, \
                mock.patch.dict(os.environ, dict(dropped, HOOKSHOT_TEST_KEPT_NOT="x", KEPT_FOR_THE_GAME="yes")):
            code, lines = prepared(stub("import json, os; print(json.dumps({'cwd': os.getcwd(), 'env': dict(os.environ)}))"),
                                   folder=folder)
            seen = json.loads(lines[0])
            self.assertEqual(code, 0)
            self.assertEqual(os.path.normcase(os.path.realpath(seen["cwd"])), os.path.normcase(os.path.realpath(folder)))
        for name in list(dropped) + ["HOOKSHOT_TEST_KEPT_NOT"]:
            self.assertNotIn(name, seen["env"], name)
        self.assertEqual(seen["env"]["KEPT_FOR_THE_GAME"], "yes")

    def test_game_env_is_the_play_buttons(self):
        env = make_game.game_env({"HOOKSHOT_X": "1", "PYTHONPATH": "p", "APPIMAGE": "a", "APPDIR": "d", "ARGV0": "0", "OWD": "o",
                                  "LD_LIBRARY_PATH": "l", "PATH": "/usr/bin", "HOME": "/home/p"})
        self.assertEqual(env, {"PATH": "/usr/bin", "HOME": "/home/p"})
        # (the shell launcher can't import it: the same names there)
        launch = (ROOT / "builder" / "linux" / "hookshot-launch").read_text(encoding="utf-8")
        names = re.search(r"\$1 ~ /\^\(([A-Z_|0-9]+)\)/ \|\| \$1 == \"LD_LIBRARY_PATH\"", launch).group(1).split("|")
        self.assertEqual(tuple(names), make_game.GAME_ENV_DROP)


class RunSilenceTests(unittest.TestCase):
    def test_a_silent_step_fails_the_build_when_checked(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), self.assertRaises(SystemExit):
            make_game.run(stub("import time; time.sleep(120)"), silence=1)
        self.assertIn("@@error", out.getvalue())
        self.assertIn("printed nothing for 1 seconds", out.getvalue())

    def test_show_filters_what_is_printed_but_not_what_is_seen(self):
        out, seen = io.StringIO(), []
        with contextlib.redirect_stdout(out):
            code = make_game.run(stub("print('keep'); print('drop')"), on_line=seen.append,
                                 show=lambda line: None if line == "drop" else line.upper())
        self.assertEqual(code, 0)
        self.assertEqual(out.getvalue().splitlines(), ["KEEP"])
        self.assertEqual(seen, ["keep", "drop"])


class SkipTests(unittest.TestCase):
    def skipped(self, environ, flag=False, cross=False):
        with mock.patch.object(make_game, "CROSS", cross):
            return make_game.prepare_skipped(SimpleNamespace(no_prepare_shaders=flag), environ)

    def test_prepared_by_default(self):
        self.assertIsNone(self.skipped({}))

    def test_the_unticked_box(self):
        self.assertIn("unticked", self.skipped({}, flag=True))

    def test_hookshot_no_prepare_as_the_game_reads_its_switches(self):
        self.assertIsNone(self.skipped({"HOOKSHOT_NO_PREPARE": "0"}))
        self.assertIsNotNone(self.skipped({"HOOKSHOT_NO_PREPARE": "1"}))
        self.assertIsNotNone(self.skipped({"HOOKSHOT_NO_PREPARE": ""}))   # (set, as the game's getenv && strcmp(.., "0"))

    def test_a_windows_game_built_on_linux(self):
        self.assertIn("Windows game built on Linux", self.skipped({}, cross=True))

    def test_the_build_times_leave_the_preparation_out(self):
        # (one check of main's order: the times are recorded and said before the preparation, which comes before @@done)
        main = SOURCE.read_text(encoding="utf-8").split("\ndef main(", 1)[1]
        self.assertLess(main.index("record_times("), main.index("prepare_skipped(a)"))
        self.assertLess(main.index('say("times"'), main.index("prepare_skipped(a)"))
        self.assertLess(main.index("prepare_skipped(a)"), main.index('say("done", out)'))


def builder_method(name):
    """One method of the Linux builder's window, compiled alone (no Tk)"""
    tree = ast.parse(BUILDER.read_text(encoding="utf-8"))
    cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "Builder")
    node = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == name)
    stages = next(n for n in tree.body if isinstance(n, ast.Assign) and getattr(n.targets[0], "id", "") == "STAGES")
    namespace = {"make_game": make_game, "time": time}
    exec(compile(ast.Module(body=[stages, node], type_ignores=[]), str(BUILDER), "exec"), namespace)
    return namespace[name]


class LinuxBuilderTests(unittest.TestCase):
    def window(self):
        text = []
        return SimpleNamespace(stage_text=SimpleNamespace(set=text.append), bar=SimpleNamespace(configure=lambda **k: None),
                               compile_started=None, eta=None, shaders_later=False), text

    def test_a_later_stage_clears_the_compiles_clock_and_countdown(self):
        on_line = builder_method("on_line")
        w, text = self.window()
        on_line(w, "@@stage compile Compiling")
        on_line(w, "@@eta 300")
        self.assertIsNotNone(w.compile_started)
        self.assertEqual(w.eta, 300)
        on_line(w, "@@stage shaders Preparing shaders")
        self.assertIsNone(w.compile_started)
        self.assertIsNone(w.eta)
        self.assertEqual(text[-1], "Preparing shaders")

    def test_the_not_prepared_line_is_noted(self):
        on_line = builder_method("on_line")
        w, _ = self.window()
        on_line(w, "the shaders were not prepared now (no usable Vulkan here): the game prepares them at its first start")
        self.assertTrue(w.shaders_later)


if __name__ == "__main__":
    unittest.main()
