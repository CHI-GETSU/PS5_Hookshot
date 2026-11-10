"""(hookshot) Each face's controller for the pause menu's controls page, drawn like the game's Pro Controller (Controller_Pro: flat
greys, a black outline), at the build from the user's RomFS: nx2/prompt_assets.py calls picture(). Its look is the drawing in each
face's entry of nx2/prompt_faces.py; a hand-made picture in nx2/prompt_faces/<face>/Controller_Pro.png takes its place.

The body is the Kenney font's controller glyph (third_party/kenney_input; the Steam Deck: deck() below, Kenney's is a shorter one),
filled, outlined and with its bumpers raised (they stop short of a touchpad); the sticks and the d-pad are the Pro Controller's own,
scaled; the face and small buttons blank discs with the face's labels (the Home button's the game's own icon). All of it drawn SS
times as large and scaled down: smooth edges.

    python -I nx2/prompt_draw.py --romfs <dir> --out <dir>   (each face's picture as <face>.png, to look at)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import prompt_assets as P  # noqa: E402
from prompt_faces import FACES  # noqa: E402

BODY, PART, INK, EDGE = (51, 51, 51, 255), (100, 100, 100, 255), (205, 205, 205, 255), (0, 0, 0, 255)
# the Pro Controller's stick and d-pad (centre, half size in its picture's pixels) and the box the body is fitted in
STICK, DPAD = (347, 228, 74), (453, 348, 70)
BOX, DECK_BOX = (151, 43, 1009, 651), (70, 105, 1088, 573)
SS = 4   # (the drawing's scale before the picture's size)


def sprite(pro, cx, cy, r):
    return pro.crop((cx - r, cy - r, cx + r + 1, cy + r + 1))


def blank_disc(r, k=1):
    """a button of the Pro, without its label (its outline k times as thick: drawn k times as large)"""
    from PIL import Image, ImageDraw
    m = 3 * k
    s = Image.new("RGBA", (2 * r + 2 * m + 1, 2 * r + 2 * m + 1), (0, 0, 0, 0))
    ImageDraw.Draw(s).ellipse((m, m, 2 * r + m, 2 * r + m), fill=PART, outline=EDGE, width=m)
    return s


def grown(mask, d):
    """an L mask (0/255) grown by d pixels all round (a blur's foot: far quicker than a max filter that size)"""
    from PIL import ImageFilter
    return mask.filter(ImageFilter.GaussianBlur(d / 2)).point(lambda v: 255 if v > 6 else 0)


def silhouette(fname, glyph, h):
    """a Kenney glyph, filled (its holes too), as an L mask"""
    from fontTools.ttLib import TTFont
    from PIL import Image, ImageChops, ImageDraw, ImageFont
    path = os.path.join(P.KENNEY, fname)
    rev = {n: c for c, n in TTFont(path).getBestCmap().items()}
    im = Image.new("L", (h * 5, h * 4), 0)
    ImageDraw.Draw(im).text((h, h), chr(rev[glyph]), font=ImageFont.truetype(path, h * 2), fill=255)
    im = im.crop(im.getbbox()).point(lambda v: 255 if v > 127 else 0)
    out = Image.new("L", (im.width + 4, im.height + 4), 255)
    out.paste(ImageChops.invert(im), (2, 2))
    ImageDraw.floodfill(out, (0, 0), 0)
    return ImageChops.lighter(im, out.crop((2, 2, im.width + 2, im.height + 2)))


def deck(w):
    """the Steam Deck's body: 2.55 times as wide as high, the grips rounder below"""
    from PIL import Image, ImageDraw
    h = round(w / 2.55)
    m = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle((0, 0, w - 1, int(h * .92)), radius=int(h * .16), fill=255)
    d.ellipse((int(w * .01), int(h * .30), int(w * .27), h - 1), fill=255)
    d.ellipse((int(w * .73), int(h * .30), int(w * .99), h - 1), fill=255)
    d.rectangle((int(w * .14), int(h * .5), int(w * .86), int(h * .92)), fill=255)
    return m


def picture(face, pro, game_icons):
    """the face's controller (RGBA, the size of pro: the game's Controller_Pro), or None for a face with no drawing"""
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
    drawing = FACES.get(face, {}).get("drawing")
    if not drawing:
        return None
    fname, glyph, controls = drawing
    W, H = pro.size[0] * SS, pro.size[1] * SS
    stick = sprite(pro, *STICK)
    dpad = sprite(pro, *DPAD)
    cross = Image.new("L", dpad.size, 0)   # (the cross alone: the square crop takes in the left stick's ring)
    ImageDraw.Draw(cross).rectangle((0, 42, 140, 98), fill=255)
    ImageDraw.Draw(cross).rectangle((44, 0, 100, 140), fill=255)
    dpad.putalpha(ImageChops.multiply(dpad.getchannel("A"), cross))
    sil = silhouette(fname, glyph, 600 * SS) if fname else deck(1600 * SS)
    box = [v * SS for v in (BOX if fname else DECK_BOX)]
    s = min((box[2] - box[0]) / sil.width, (box[3] - box[1]) / sil.height) * 0.97
    sil = sil.resize((round(sil.width * s), round(sil.height * s)), Image.LANCZOS).point(lambda v: 255 if v > 127 else 0)
    ox, oy = (W - sil.width) // 2, box[1] + (box[3] - box[1] - sil.height) // 2
    mask = Image.new("L", (W, H), 0)
    mask.paste(sil, (ox, oy))
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if fname:   # (the bumpers: the body raised, an ellipse over each top corner)
        up = Image.new("L", (W, H), 0)
        up.paste(mask, (0, -18 * SS))
        top = Image.new("L", (W, H), 0)
        for c in (.25, .75):
            ImageDraw.Draw(top).ellipse((ox + int(sil.width * (c - .17)), oy - int(sil.height * .12), ox + int(sil.width * (c + .17)),
                                         oy + int(sil.height * .16)), fill=255)
        for kind, cx, _, size, _ in controls:
            if kind == "pad":   # (a touchpad's top edge is no bumper)
                ImageDraw.Draw(top).rectangle((ox + int(sil.width * (cx - size[0] / 2 - .03)), 0,
                                               ox + int(sil.width * (cx + size[0] / 2 + .03)), H), fill=0)
        bump = ImageChops.multiply(up, top).filter(ImageFilter.GaussianBlur(SS)).point(lambda v: 255 if v > 127 else 0)
        im.paste(EDGE, (0, 0), grown(bump, 3 * SS))
        im.paste(PART, (0, 0), bump)
    im.paste(EDGE, (0, 0), grown(mask, 3 * SS))
    im.paste(BODY, (0, 0), mask)
    icons = P.Icons(face, None)
    d = ImageDraw.Draw(im)
    for kind, cx, cy, size, what in controls:
        x, y = ox + cx * sil.width, oy + cy * sil.height
        if kind in ("stick", "dpad"):
            r = round(size * sil.width / 2)
            im.alpha_composite((stick if kind == "stick" else dpad).resize((2 * r + 1, 2 * r + 1), Image.LANCZOS),
                               (round(x) - r, round(y) - r))
        elif kind == "screen":
            w2, h2 = size[0] * sil.width / 2, size[1] * sil.height / 2
            d.rounded_rectangle((x - w2, y - h2, x + w2, y + h2), radius=8 * SS, fill=(27, 27, 27, 255), outline=EDGE, width=3 * SS)
            d.rectangle((x - w2 + 22 * SS, y - h2 + 22 * SS, x + w2 - 22 * SS, y + h2 - 22 * SS), fill=(77, 77, 77, 255),
                        outline=EDGE, width=2 * SS)
        elif kind == "pad":
            w2, h2 = size[0] * sil.width / 2, size[1] * sil.height / 2
            d.rounded_rectangle((x - w2, y - h2, x + w2, y + h2), radius=round(min(w2, h2) * .35), fill=PART, outline=EDGE,
                                width=3 * SS)
        else:   # ("face", "small": a disc with the face's label, the Home button the game's icon)
            r = round(size * sil.width / 2)
            im.alpha_composite(blank_disc(r, SS), (round(x) - r - 3 * SS, round(y) - r - 3 * SS))
            ic = game_icons if what == "guide" else icons
            if what in ic.code:
                n = int(2 * r * (.92 if kind == "face" else .80))
                _, shape, holes = ic.masks(what, n)
                full = sum(i * k for i, k in enumerate(shape.histogram())) / 255 / n / n > 0.7   # (a disc with a letter in it)
                mark = holes if full else ic.masks(what, int(n * (.7 if kind == "face" else .85)))[0]
                im.paste(INK, (round(x) - mark.width // 2, round(y) - mark.height // 2), mark)
    return im.resize(pro.size, Image.BOX)


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--romfs", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    dictionary = P.zs_dictionary(a.romfs)
    b = P.read_zs(a.romfs, P.TEXTURES, dictionary)
    pro = P.decode(b, P.bntx_textures(b)["Controller_Pro"]).convert("RGBA")
    game_icons = P.Icons("switch", P.unwrap(P.read_zs(a.romfs, P.FONT, dictionary))[1])
    os.makedirs(a.out, exist_ok=True)
    for face in FACES:
        im = picture(face, pro, game_icons)
        if im:
            im.save(os.path.join(a.out, f"{face}.png"))
            print(f"prompts: {face}.png", file=sys.stderr)


if __name__ == "__main__":
    main()
