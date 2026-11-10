"""Validate binary capture adjacency without optional image-analysis packages."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "framelog.py"
spec = importlib.util.spec_from_file_location("framelog", SCRIPT)
log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(log)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="hookshot-framelog-")
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name) / "frames.rgba"

    def capture(self, sequences, timestamps=None, kinds=None):
        timestamps = timestamps if timestamps is not None else range(1, len(sequences) + 1)
        kinds = kinds if kinds is not None else [0] * len(sequences)
        with self.path.open("wb") as file:
            for t, s, k in zip(timestamps, sequences, kinds):
                file.write(struct.pack("<QII", t, s, k))
                file.write(bytes(log.SIZE - 16))

    def test_adjacency_and_wrap(self):
        for sequences, missing, centres in [
                ([0, 1, 2], 0, [1]), ([0, 1, 3], 1, []),
                ([0, 1, 3, 4, 5], 1, [3]),
                ([log.MASK - 1, log.MASK, 0], 0, [1]),
                ([log.MASK - 1, 0, 1], 1, [])]:
            with self.subTest(sequences=sequences):
                self.capture(sequences)
                info = log.capture_info(self.path)
                self.assertEqual((info.missing, info.centres), (missing, centres))

    def test_short_and_truncated_captures(self):
        for count in range(3):
            self.capture(range(count))
            with self.assertRaisesRegex(ValueError, "need at least 3"):
                log.capture_info(self.path)
        self.capture([0, 1, 2])
        with self.path.open("ab") as file:
            file.write(b"partial")
        with self.assertRaisesRegex(ValueError, "truncated record 4"):
            log.capture_info(self.path)

    def test_invalid_headers(self):
        for sequences, timestamps, kinds, error in [
                ([0, 1, 2], [1, 2, 3], [0, 2, 1], "invalid frame kind"),
                ([0, 1, 2], [1, 1, 3], [0, 0, 0], "timestamp"),
                ([0, 1, 2], [2, 1, 3], [0, 0, 0], "timestamp"),
                ([0, 0, 2], [1, 2, 3], [0, 0, 0], "sequence"),
                ([2, 1, 3], [1, 2, 3], [0, 0, 0], "sequence")]:
            with self.subTest(error=error, sequences=sequences):
                self.capture(sequences, timestamps, kinds)
                with self.assertRaisesRegex(ValueError, error):
                    log.capture_info(self.path)

    def test_isolated_cli_and_legacy_output_argument(self):
        self.capture([0, 1, 3])
        result = subprocess.run([sys.executable, "-I", "-B", str(SCRIPT), str(self.path),
                                 str(self.path.with_suffix(".png")), "--inspect"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 eligible, 1 skipped", result.stdout)
        self.assertIn("missing 1 frames", result.stdout)
        self.assertFalse(self.path.with_suffix(".png").exists())
        self.capture([])
        result = subprocess.run([sys.executable, "-I", "-B", str(SCRIPT), str(self.path), "--inspect"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("need at least 3", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
