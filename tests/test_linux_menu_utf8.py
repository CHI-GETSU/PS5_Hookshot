"""Exercise Linux GDI measurement and rasterization with UTF-8 menu text."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
#include "gdi_posix.c"

DWORD GetModuleFileNameA(HMODULE module, LPSTR out, DWORD cap) {
    (void)module;
    return (DWORD)snprintf(out, cap, "%s", getenv("TEST_PROGRAM"));
}

static int check_width(GDc *dc, const char *s, int n, const int *points, int count) {
    Face *face = face_of(dc->font);
    const float scale = scale_of(face, dc->font);
    float expected = 0;
    for (int i = 0; i < count; i++) {
        if (i) expected += scale * stbtt_GetCodepointKernAdvance(&face->info, points[i - 1], points[i]);
        int advance, bearing;
        stbtt_GetCodepointHMetrics(&face->info, points[i], &advance, &bearing);
        expected += advance * scale;
    }
    SIZE measured;
    GetTextExtentPoint32A(dc, s, n, &measured);
    if (measured.cx == (int)ceilf(expected)) return 1;
    fprintf(stderr, "measured %ld pixels, expected %d\n", (long)measured.cx, (int)ceilf(expected));
    return 0;
}

int main(void) {
    enum { width = 64, height = 64, x = 8, y = 40 };
    uint32_t bits[width * height] = {0};
    GBitmap bitmap = {.w = width, .h = height, .bits = bits};
    GFont font = {.px = 32, .aa = 1};
    GDc dc = {.bmp = &bitmap, .font = &font, .text = RGB(255, 255, 255), .align = TA_BASELINE};
    Face *face = face_of(&font);
    if (!face || !stbtt_FindGlyphIndex(&face->info, 0xe9)) return 1;

    const char label[] = "Micr\xc3\xb3" "fono";
    const int points[] = {'M', 'i', 'c', 'r', 0xf3, 'f', 'o', 'n', 'o'};
    if (!check_width(&dc, label, sizeof label - 1, points, sizeof points / sizeof *points)) return 1;
    const int four_byte[] = {0x1f600, 'A'};
    if (!check_width(&dc, "\xf0\x9f\x98\x80" "A", 5, four_byte, 2)) return 1;
    const int invalid[] = {0xfffd, 'A'};
    if (!check_width(&dc, "\xe2" "A", 2, invalid, 2)) return 1;

    TextOutA(&dc, x, y, "\xc3\xa9", 2);
    const float scale = scale_of(face, &font);
    int gw, gh, xoff, yoff;
    unsigned char *glyph = stbtt_GetCodepointBitmap(&face->info, scale, scale, 0xe9, &gw, &gh, &xoff, &yoff);
    if (!glyph || gw <= 0 || gh <= 0 || x + xoff < 0 || y + yoff < 0 ||
        x + xoff + gw > width || y + yoff + gh > height) return 1;
    uint32_t expected[width * height] = {0};
    for (int gy = 0; gy < gh; gy++)
        for (int gx = 0; gx < gw; gx++)
            expected[(y + yoff + gy) * width + x + xoff + gx] = glyph[gy * gw + gx] * 0x010101u;
    stbtt_FreeBitmap(glyph, NULL);
    if (!memcmp(bits, expected, sizeof bits)) return 0;
    fprintf(stderr, "UTF-8 draw did not match the font's e-acute glyph\n");
    return 1;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="hookshot-utf8-") as temp:
        root = Path(temp)
        fonts = root / "fonts"
        fonts.mkdir()
        bundled = ROOT / "builder/HookshotBuilder/Fonts"
        for kind in ("regular", "bold"):
            (fonts / (kind + ".ttf")).symlink_to(bundled / ("Inter-" + kind.capitalize() + ".ttf"))
        source = root / "fixture.c"
        source.write_text(FIXTURE)
        exe = root / "fixture"
        subprocess.run([args.cc, "-std=c11", "-I" + str(ROOT / "host/posix"),
                        "-I" + str(ROOT / "third_party/stb"), str(source),
                        "-o", str(exe), "-pthread", "-lm"], check=True)
        subprocess.run([str(exe)], env=dict(os.environ, TEST_PROGRAM=str(root / "hookshot")), check=True)


if __name__ == "__main__":
    main()
