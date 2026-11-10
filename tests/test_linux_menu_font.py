"""Check the face selected by Linux's actual menu font loader, without a game or GPU."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "builder" / "HookshotBuilder" / "Fonts"
FIXTURE = r'''
#include "gdi_posix.c"

DWORD GetModuleFileNameA(HMODULE module, LPSTR out, DWORD cap) {
    (void)module;
    if (cap) out[0] = 0; /* no game-local font override */
    return 0;
}

int main(void) {
    faces_load();
    for (int face = 0; face < 2; face++) {
        if (!faces[face].ok ||
            faces[face].info.fontstart != stbtt_GetFontOffsetForIndex(faces[face].data, face)) {
            fprintf(stderr, "fontconfig selected collection face %d, but it was not loaded\n", face);
            return 1;
        }
    }
    return 0;
}
'''


def collection():
    """Put two bundled fonts in a test TTC. Its table offsets are relative to the collection."""
    data = bytearray(struct.pack(">4sII2I", b"ttcf", 0x10000, 2, 0, 0))
    for index, name in enumerate(("Inter-Regular.ttf", "Inter-Bold.ttf")):
        font = bytearray((FONTS / name).read_bytes())
        start = len(data)
        struct.pack_into(">I", data, 12 + 4 * index, start)
        count = struct.unpack_from(">H", font, 4)[0]
        for entry in range(count):
            at = 12 + 16 * entry + 8
            offset = struct.unpack_from(">I", font, at)[0]
            struct.pack_into(">I", font, at, start + offset)
        data.extend(font)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="hookshot-font-") as temp:
        root = Path(temp)
        font = root / "menu font.ttc"
        font.write_bytes(collection())
        match = root / "fc-match"
        match.write_text("""#!/bin/sh
case "$2" in
  *index*)
    case "$3" in
      *bold*) index=65537 ;;
      *) index=0 ;;
    esac
    printf '%s\\n' "$index"
    ;;
esac
printf '%s' "$TEST_FONT"
""")
        match.chmod(0o755)
        source = root / "fixture.c"
        source.write_text(FIXTURE)
        exe = root / "fixture"
        subprocess.run([args.cc, "-std=c11", "-I" + str(ROOT / "host/posix"),
                        "-I" + str(ROOT / "third_party/stb"), str(source),
                        "-o", str(exe), "-pthread", "-lm"], check=True)
        env = dict(os.environ, TEST_FONT=str(font), PATH=str(root) + os.pathsep + os.environ["PATH"])
        subprocess.run([str(exe)], env=env, check=True)


if __name__ == "__main__":
    main()
