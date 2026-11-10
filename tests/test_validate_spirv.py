"""scripts/validate_spirv.py finds a program cache record's stages and tells why a stage would fail.

A cache file is made here with a mesh-as-compute record of two compute stages: a valid module whose workgroup memory
(80000 bytes) is over RDNA1's 64 KB, and the same module with its array's element type left undefined. The stages are
found by their magic words between the record's other fields, the local size and workgroup memory are read from the
module, and (when the Vulkan SDK's spirv-val is there) the run flags both stages and writes them out.

python -I -B tests/test_validate_spirv.py
"""
import importlib.util
import os
import struct
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("validate_spirv", ROOT / "scripts" / "validate_spirv.py")
vs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vs)


def op(code, *args):
    return [(len(args) + 1) << 16 | code, *args]


def module(element=3):
    """OpCapability Shader; GLCompute main, LocalSize 64 1 1; a Workgroup uint[20000]"""
    w = [0x07230203, 0x00010000, 0, 10, 0]
    w += op(17, 1) + op(14, 0, 1)
    w += op(15, 5, 8, int.from_bytes(b"main", "little"), 0)
    w += op(16, 8, 17, 64, 1, 1)
    w += op(19, 1) + op(33, 2, 1) + op(21, 3, 32, 0) + op(43, 3, 4, 20000)
    w += op(28, 5, element, 4) + op(32, 6, 4, 5) + op(59, 6, 7, 4)
    w += op(54, 1, 8, 0, 2) + op(248, 9) + op(253) + op(56)
    return struct.pack("<%dI" % len(w), *w)


def record(*spirvs):
    data = struct.pack("<I", vs.FALLBACK | 2) + struct.pack("<Q", 0) + struct.pack("<3I", 64, 1, 1) + bytes(24)
    data += struct.pack("<Q", len(spirvs))
    for nvn, s in enumerate(spirvs, 6):   # (the task stage, then the mesh stage)
        data += struct.pack("<iI", nvn, 0x20) + struct.pack("<Q", len(s) // 4) + s + struct.pack("<Q", 0) + bytes(40)
    return data


def main():
    good, bad = module(), module(element=99)
    data = record(good, bad)
    found = vs.stages(data)
    assert found == [(6, 0x20, good), (7, 0x20, bad)], found
    assert vs.reflect(good) == ((64, 1, 1), 80000), vs.reflect(good)

    with tempfile.TemporaryDirectory() as tmp:
        cache = os.path.join(tmp, "hookshot_programs.bin")
        with open(cache, "wb") as f:
            f.write(struct.pack("<IIQ", vs.CACHE_MAGIC, 0, 1))
            f.write(struct.pack("<QQ", 0xA5DA9F84B72D1471, len(data)) + data)
            other = struct.pack("<I", 0) + bytes(8)   # (an ordinary program: left out unless --all)
            f.write(struct.pack("<QQ", 0x1234, len(other)) + other)
        keys = [k for k, _, _ in vs.records(cache, lambda key, kind: bool(kind & vs.FALLBACK))]
        assert keys == [0xA5DA9F84B72D1471], keys

        if vs.find_tool("spirv-val"):
            out = os.path.join(tmp, "out")
            status = vs.main([cache, "--out", out, "--jobs", "2"])
            assert status == 1, status
            names = sorted(os.listdir(out))
            assert {"a5da9f84b72d1471_6_compute.spv", "a5da9f84b72d1471_7_compute.spv"} <= set(names), names
            text = Path(out, "a5da9f84b72d1471_6_compute.txt").read_text(encoding="utf-8")
            assert "maxComputeSharedMemorySize" in text and "[spirv-val]" not in text, text
            text = Path(out, "a5da9f84b72d1471_7_compute.txt").read_text(encoding="utf-8")
            assert "[spirv-val]" in text, text
            assert vs.main([cache, "--out", out, "--key", "0000000000001234"]) == 0
            print("validate_spirv: stages, reflection and spirv-val ok")
        else:
            print("validate_spirv: stages and reflection ok (spirv-val not found: its run skipped)")


if __name__ == "__main__":
    main()
