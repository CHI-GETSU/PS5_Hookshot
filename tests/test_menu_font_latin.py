"""The Insert menu's font on Linux has Latin letters (host/posix/gdi_posix.c faces_load, issue 1279).

The menu drew with whatever font loaded first: one beside the game, then fontconfig's sans-serif, then a few usual
paths. A font with no Latin letters drew every letter of the menu as its empty box (.notdef). A font without a glyph
for 'A' is now passed over, and fontconfig is asked for a font with English's letters (sans-serif:lang=en).

Two tiny TrueType fonts are made here: one with 'A', one with only a Hebrew letter. The one without Latin is put beside
the game (fonts/regular.ttf and fonts/bold.ttf, the first ones tried), and a stand-in fc-match on PATH answers with the
Latin one. gdi_posix.c is compiled with a fixture that draws "A" in both weights: each must draw pixels, and fc-match
must have been asked with lang=en. No system font is used.

    python -I -B tests/test_menu_font_latin.py [--cc gcc]
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def font(codepoint):
    """A TrueType font with an empty .notdef and one square glyph, mapped from codepoint"""
    square = struct.pack(">hhhhh", 1, 100, 0, 900, 800) + struct.pack(">HH", 3, 0) + bytes([1] * 4)
    square += struct.pack(">hhhh", 100, 800, 0, -800) + struct.pack(">hhhh", 0, 0, 800, 0)   # (x deltas, y deltas)
    square += b"\0" * (-len(square) % 4)
    glyf = square
    loca = struct.pack(">HHH", 0, 0, len(square) // 2)   # (short offsets: halved)
    head = struct.pack(">IIIIHHqqhhhhHHhhh", 0x10000, 0x10000, 0, 0x5F0F3CF5, 0, 1000, 0, 0, 0, 0, 1000, 1000, 0, 8, 2, 0, 0)
    hhea = struct.pack(">IhhhHhhhhhhhhhhhH", 0x10000, 800, -200, 0, 1000, 0, 0, 1000, 1, 0, 0, 0, 0, 0, 0, 0, 2)
    maxp = struct.pack(">IH", 0x5000, 2)
    hmtx = struct.pack(">HhHh", 500, 0, 1000, 100)
    segs = [(codepoint, codepoint, (1 - codepoint) & 0xFFFF), (0xFFFF, 0xFFFF, 1)]
    n = len(segs)
    sub = struct.pack(">HHHHHHH", 4, 16 + 8 * n, 0, 2 * n, 2, 0, 2 * n - 2)
    sub += b"".join(struct.pack(">H", s[1]) for s in segs) + b"\0\0"
    sub += b"".join(struct.pack(">H", s[0]) for s in segs)
    sub += b"".join(struct.pack(">H", s[2]) for s in segs) + b"\0\0" * n
    sub = sub[:2] + struct.pack(">H", len(sub)) + sub[4:]
    cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + sub
    tables = sorted({b"cmap": cmap, b"glyf": glyf, b"head": head, b"hhea": hhea, b"hmtx": hmtx, b"loca": loca,
                     b"maxp": maxp}.items())
    out = struct.pack(">IHHHH", 0x10000, len(tables), 0, 0, 0)
    offset = 12 + 16 * len(tables)
    body = b""
    for tag, data in tables:
        out += tag + struct.pack(">III", 0, offset + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    return out + body


FIXTURE = r"""
#include <windows.h>
#include <stdio.h>
#include <string.h>
static const char *module_path;
DWORD GetModuleFileNameA(HMODULE m, LPSTR out, DWORD cap) {
    (void)m;
    snprintf(out, cap, "%s", module_path);
    return (DWORD)strlen(out);
}
static int inked(int weight) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 64, bi.bmiHeader.biHeight = -64, bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dc, &bi, 0, &bits, NULL, 0);
    HFONT f = CreateFontA(-32, 0, 0, 0, weight, 0, 0, 0, 0, 0, 0, 0, 0, "");
    SelectObject(dc, bmp), SelectObject(dc, f);
    SetTextColor(dc, 0xFFFFFF);
    TextOutA(dc, 8, 8, "A", 1);
    int n = 0;
    for (int i = 0; i < 64 * 64; i++) n += ((unsigned *)bits)[i] != 0;
    DeleteObject(f), DeleteObject(bmp), DeleteDC(dc);
    return n;
}
int main(int argc, char **argv) {
    module_path = argc > 1 ? argv[1] : "";
    printf("regular %d bold %d\n", inked(FW_NORMAL), inked(FW_BOLD));
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    if os.name == "nt":
        print("SKIP: the Linux menu's text (host/posix)")
        return
    with tempfile.TemporaryDirectory(prefix="menu-font-") as tmp:
        tmp = Path(tmp)
        (tmp / "bin" / "fonts").mkdir(parents=True)
        (tmp / "tools").mkdir()
        latin = tmp / "latin.ttf"
        latin.write_bytes(font(ord("A")))
        for name in ("regular.ttf", "bold.ttf"):
            (tmp / "bin" / "fonts" / name).write_bytes(font(0x05D0))   # (Hebrew alef only)
        asked = tmp / "fc-match-args"
        fc = tmp / "tools" / "fc-match"
        fc.write_text('#!/bin/sh\necho "$@" >> "%s"\nprintf %%s "%s"\n' % (asked, latin), encoding="utf-8")
        fc.chmod(0o755)
        src, exe = tmp / "fixture.c", tmp / "fixture"
        src.write_text(FIXTURE, encoding="utf-8")
        r = subprocess.run([args.cc, "-std=gnu11", "-O1", "-I", str(ROOT / "host" / "posix"), "-I", str(ROOT / "third_party" / "stb"),
                            str(src), str(ROOT / "host" / "posix" / "gdi_posix.c"), "-o", str(exe), "-lm", "-pthread"],
                           capture_output=True, text=True, timeout=120)
        if r.returncode:
            print(r.stdout + r.stderr)
            raise SystemExit("the fixture did not build")
        env = dict(os.environ, PATH=str(tmp / "tools") + os.pathsep + os.environ.get("PATH", ""))
        r = subprocess.run([str(exe), str(tmp / "bin" / "hookshot")], capture_output=True, text=True, timeout=30, env=env)
        print(r.stdout + r.stderr, end="")
        words = r.stdout.split()
        if r.returncode or len(words) != 4 or not (int(words[1]) > 0 and int(words[3]) > 0):
            raise SystemExit("FAIL: the menu drew 'A' with a font that has no Latin letters (empty boxes)")
        calls = asked.read_text(encoding="utf-8").splitlines() if asked.exists() else []
        if not calls or not all("lang=en" in c for c in calls):
            raise SystemExit("FAIL: fontconfig was not asked for a font with English's letters: %s" % calls)
    print("PASS: a font without Latin letters is passed over, and fontconfig is asked with lang=en")


if __name__ == "__main__":
    sys.exit(main())
