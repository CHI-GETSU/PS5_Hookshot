"""Which pass made a flash: reads a probe_<frame>.txt (vk_draw.cpp RX_VK_PROBE) and aligns the frames before, at and
after it by (kind, program, target, nth use), then prints the entries of the flash frame in order whose colour is far
from both neighbours', decoded by format. The comparison skips decoded NaNs.
The first reported entry is the pass that went wrong.

    python -I scripts/probe.py work/runs/<run>/probe_<frame>.txt [ratio 4] [lines 25]
"""
import collections, math, struct, sys


def half(h):
    return struct.unpack("<e", struct.pack("<H", h))[0]


def uf(v, mbits):
    e, m = v >> mbits, v & ((1 << mbits) - 1)
    return (m / (1 << mbits)) * 2.0 ** -14 if e == 0 else 1e30 if e == 31 else (1 + m / (1 << mbits)) * 2.0 ** (e - 15)


def decode(fmt, w):
    if fmt == 122:   # B10G11R11
        return [uf(w[0] & 2047, 6), uf((w[0] >> 11) & 2047, 6), uf(w[0] >> 22, 5)]
    if fmt == 123:   # E5B9G9R9
        s = 2.0 ** ((w[0] >> 27) - 24)
        return [(w[0] & 511) * s, ((w[0] >> 9) & 511) * s, ((w[0] >> 18) & 511) * s]
    if fmt == 97:    # R16G16B16A16_SFLOAT
        return [half(w[0] & 0xffff), half(w[0] >> 16), half(w[1] & 0xffff)]
    if fmt == 83:    # R16G16_SFLOAT
        return [half(w[0] & 0xffff), half(w[0] >> 16)]
    if fmt == 76:    # R16_SFLOAT
        return [half(w[0] & 0xffff)]
    if fmt == 100:   # R32_SFLOAT
        return list(struct.unpack("<f", struct.pack("<I", w[0])))
    if fmt == 109:   # R32G32B32A32_SFLOAT
        return list(struct.unpack("<4f", struct.pack("<4I", *w)))
    return [(w[0] >> k) & 255 for k in (0, 8, 16, 24)]


def key_rows(rows):
    seen, out = collections.Counter(), {}
    for r in rows:
        k = (r[2], r[3], r[4])
        out[k + (seen[k],)] = r
        seen[k] += 1
    return out


frames = collections.OrderedDict()
with open(sys.argv[1], encoding="utf-8") as capture:
    for line_number, line in enumerate(capture, 1):
        r = line.split()
        if not r:
            continue
        if len(r) < 11:
            raise SystemExit(f"Probe capture line {line_number} is incomplete. Capture a new probe_*.txt file.")
        frames.setdefault(r[0], []).append(r)
if len(frames) != 3:
    raise SystemExit(f"Probe capture expected 3 frames, found {len(frames)}. "
                     "Capture a new probe_*.txt file with the frame before, the flash, and the frame after.")
a, b, c = [key_rows(v) for v in frames.values()]
ratio = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
lines = int(sys.argv[3]) if len(sys.argv) > 3 else 25


def val(r):
    return decode(int(r[6]), [int(x, 16) for x in r[7:11]])


def far(x, y):
    for p, q in zip(x, y):
        if math.isnan(p) or math.isnan(q):
            continue
        hi, lo = max(abs(p), abs(q)), min(abs(p), abs(q))
        if hi > 1e-3 and hi > lo * ratio:
            return True
    return False


print("frames", list(frames), {k: len(v) for k, v in frames.items()}, "unmatched at the flash:", sum(1 for k in b if k not in a or k not in c))
told = 0
for k, r in b.items():
    if k not in a or k not in c:
        continue
    va, vb, vc = val(a[k]), val(r), val(c[k])
    if far(va, vb) and far(vc, vb):
        print("%4s %s prog %s %s %s f%s: %s | %s | %s" % (r[1], r[2], r[3][:8], r[4], r[5], r[6], ["%.4g" % x for x in va], ["%.4g" % x for x in vb], ["%.4g" % x for x in vc]))
        told += 1
        if told >= lines:
            break
print("%d entries far from both neighbours" % told)
