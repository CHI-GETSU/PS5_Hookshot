"""Game-free: the Linux AppImage carries the controller fonts the button prompts are drawn from (third_party/kenney_input,
nx2/prompt_assets.py KENNEY). Without them make_game.py says the prompts "were not made" and the game keeps the Switch's icons,
even with fontTools installed.

python -I tests/test_appimage_prompt_fonts.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    script = (ROOT / "scripts/linux/appimage.sh").read_text(encoding="utf-8")
    assets = (ROOT / "nx2/prompt_assets.py").read_text(encoding="utf-8")
    m = re.search(r'KENNEY = os\.path\.join\(os\.path\.dirname\(HERE\), "third_party", "([^"]+)"\)', assets)
    if not m:
        raise SystemExit("FAIL: nx2/prompt_assets.py has no KENNEY under third_party")
    folder = "third_party/" + m.group(1)
    if not (ROOT / folder).is_dir():
        raise SystemExit("FAIL: %s does not exist" % folder)
    if not re.search(r'^cp -a %s "\$T/third_party/"$' % re.escape(folder), script, re.M):
        raise SystemExit("FAIL: scripts/linux/appimage.sh does not copy %s" % folder)
    print("ok: the AppImage copies %s" % folder)


if __name__ == "__main__":
    sys.exit(main())
