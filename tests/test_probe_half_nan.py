"""Exercise flash detection and capture errors with authored probe samples."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "probe.py"


class ProbeTests(unittest.TestCase):
    def run_probe(self, rows):
        with tempfile.TemporaryDirectory(prefix="hookshot-probe-") as folder:
            trace = Path(folder) / "probe_1.txt"
            trace.write_text("".join(rows), encoding="utf-8")
            return subprocess.run([sys.executable, "-I", "-B", str(SCRIPT), str(trace)],
                                  capture_output=True, text=True)

    def test_nan_is_not_a_brightness_jump(self):
        rows = []
        for frame in range(3):
            # R16_SFLOAT: 0x3c00 is 1.0 and 0x7e00 is NaN.
            nan_sample = 0x7E00 if frame == 1 else 0x3C00
            rows.append(f"{frame} 0 D abcd nan_only 1x1 76 {nan_sample:08x} 0 0 0\n")
            # R16G16_SFLOAT: a NaN in R must not hide a real jump in G.
            jump_sample = 0x4C007E00 if frame == 1 else 0x3C003C00
            rows.append(f"{frame} 1 D abcd real_jump 1x1 83 {jump_sample:08x} 0 0 0\n")
            rows.append("\n")
        result = self.run_probe(rows)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("nan_only", result.stdout)
        self.assertIn("real_jump", result.stdout)
        self.assertIn("['nan', '16']", result.stdout)
        self.assertIn("\n1 entries far from both neighbours\n", result.stdout)

    def test_reports_incorrect_frame_count(self):
        for count in (0, 1, 2, 4):
            with self.subTest(count=count):
                rows = [f"{frame} 0 D abcd target 1x1 76 00003c00 0 0 0\n" for frame in range(count)]
                result = self.run_probe(rows)
                self.assertEqual(result.returncode, 1)
                self.assertIn(f"expected 3 frames, found {count}", result.stderr)
                self.assertIn("Capture a new probe_", result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_reports_truncated_row_with_line_number(self):
        rows = ["0 0 D abcd target 1x1 76 00003c00 0 0 0\n", "\n", "1 0 D abcd target 1x1 76 00003c00\n",
                "2 0 D abcd target 1x1 76 00003c00 0 0 0\n"]
        result = self.run_probe(rows)
        self.assertEqual(result.returncode, 1)
        self.assertIn("line 3 is incomplete", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
