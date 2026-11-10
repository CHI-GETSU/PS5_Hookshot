"""Authored Python-part fixtures: the prompt encoder's dependency must be bundled without pip dependency resolution."""
from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REQUIREMENTS = ROOT / "nx2/prompt_requirements.txt"
spec = importlib.util.spec_from_file_location("release_parts", ROOT / "scripts/release_parts.py")
release_parts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release_parts)


class PromptPackages(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="hookshot-prompt-packages-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.part = self.root / "authored-python.zip"
        self.tools = self.root / "tools"
        self.pins = [line.split("#")[0].strip() for line in REQUIREMENTS.read_text(encoding="utf-8").splitlines()]
        self.pins = [line for line in self.pins if line]

    def python_part(self, pins):
        with zipfile.ZipFile(self.part, "w") as archive:
            prefix = "HookshotBuilder/tools/python/"
            # A marker only, never loaded: the filename tells the packager the target Python version.
            archive.writestr(prefix + "python314.dll", b"authored version marker")
            for pin in pins:
                name, version = pin.split("==")
                folder = name.replace("-", "_") + "-" + version + ".dist-info"
                metadata = "Metadata-Version: 2.1\nName: %s\nVersion: %s\n" % (name, version)
                if name == "astc-encoder-py":
                    metadata += "Requires-Dist: archspec >=0.2\n"
                archive.writestr(prefix + "Lib/site-packages/" + folder + "/METADATA", metadata)

    def package(self):
        with patch.object(release_parts, "named_part", return_value=str(self.part)), \
                patch.object(release_parts, "get", side_effect=AssertionError("no network in this fixture")), \
                patch.object(release_parts, "extract", wraps=release_parts.extract) as extracted, \
                patch.object(release_parts.subprocess, "run") as pip, redirect_stdout(io.StringIO()):
            self.assertEqual(release_parts.python_packages("authored-setup.txt", str(REQUIREMENTS), str(self.tools)), 0)
            return pip, extracted

    def test_encoder_present_without_archspec_installs_the_pinned_dependency(self):
        # This matches an existing Python part: the encoder itself is present but cannot import.
        self.python_part(pin for pin in self.pins if not pin.startswith("archspec=="))
        pip, extracted = self.package()
        pip.assert_called_once()
        command = pip.call_args.args[0]
        self.assertIn("--no-deps", command)
        self.assertEqual(command[command.index("--only-binary") + 1], ":all:")
        self.assertEqual(command[command.index("--platform") + 1], "win_amd64")
        self.assertEqual(command[command.index("--python-version") + 1], "3.14")
        target = command.index("--target")
        self.assertEqual(Path(command[target + 1]), self.tools / "python/Lib/site-packages")
        requested = command[target + 2:]
        self.assertEqual(len(requested), 1)
        name, version = requested[0].split("==")
        self.assertEqual(name, "archspec")
        self.assertGreaterEqual(tuple(map(int, version.split("."))), (0, 2))
        self.assertIn(requested[0], self.pins)
        self.assertEqual(pip.call_args.kwargs, {"check": True})
        extracted.assert_called_once()
        self.assertEqual((self.tools / "python/python314.dll").read_bytes(), b"authored version marker")

    def test_complete_python_part_is_reused_without_pip_or_extraction(self):
        self.python_part(self.pins)
        pip, extracted = self.package()
        pip.assert_not_called()
        extracted.assert_not_called()
        self.assertFalse(self.tools.exists())


if __name__ == "__main__":
    unittest.main()
