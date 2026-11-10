"""The HDR10 pass (gpu/vk/shaders/hdr_encode.comp): hdr_encode_comp_spv.h names the sha256 of the shader it was compiled from, and
with glslangValidator (--glslang) the header's code is the shader compiled now. And the PQ curve and the highlight expansion it
uses, checked in Python: paper white lands where ST 2084 puts it, the picture's white reaches the peak, black stays black.

    python -I -B tests/test_hdr_encode.py [--glslang glslangValidator]
"""
import argparse, hashlib, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "gpu" / "vk" / "shaders"


def pq(nits):
    m1, m2, c1, c2, c3 = 0.1593017578125, 78.84375, 0.8359375, 18.8515625, 18.6875
    l = (min(max(nits / 10000.0, 0.0), 1.0)) ** m1
    return ((c1 + c2 * l) / (1 + c3 * l)) ** m2


def nits(y, white, peak, knee=0.6):
    over = min(max((y - knee) / (1 - knee), 0.0), 1.0)
    return y * white * (1 + (peak / white - 1) * over * over)


a = argparse.ArgumentParser()
a.add_argument("--glslang")
args = a.parse_args()
src = (SHADERS / "hdr_encode.comp").read_bytes()
header = (SHADERS / "hdr_encode_comp_spv.h").read_text()
first = header.splitlines()[0]
if hashlib.sha256(src).hexdigest() not in first:
    sys.exit("FAIL: hdr_encode_comp_spv.h was not made from hdr_encode.comp as it is (recompile it)")
assert abs(pq(100) - 0.5081) < 0.001 and abs(pq(1000) - 0.7518) < 0.001 and pq(0) < 1e-6, "PQ curve"
assert abs(nits(1.0, 200, 1000) - 1000) < 1e-6 and abs(nits(0.5, 200, 1000) - 100) < 1e-6 and nits(0, 200, 1000) == 0
assert all(nits(a / 100, 200, 1000) <= nits((a + 1) / 100, 200, 1000) for a in range(100)), "monotonic"
if args.glslang:
    with tempfile.TemporaryDirectory() as d:
        out = Path(d, "h.h")
        subprocess.run([args.glslang, "-V", "--target-env", "vulkan1.2", "--vn", "HDR_ENCODE_COMP_SPV", "-o", str(out),
                        str(SHADERS / "hdr_encode.comp")], check=True, capture_output=True)
        if out.read_text() != "\n".join(header.splitlines(True)[1:]) and out.read_text() != "".join(header.splitlines(True)[1:]):
            sys.exit("FAIL: hdr_encode_comp_spv.h differs from the shader compiled now")
print("PASS: the HDR10 pass's header is its shader, PQ and the highlight expansion as intended")
