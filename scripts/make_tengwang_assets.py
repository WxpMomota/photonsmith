"""make_tengwang_assets.py -- the pictures "Preface to the Pavilion of Prince Teng" (src/shared/cartoon/tengwang*.h) brings with it, in pbrt_scenes/film_assets/tengwang:

  plaque.jpg          the Pavilion's name board, gold on blue
  banner_li.jpg       the general Li Guang's banner
  manuscript_N.jpg    the paper Wang Bo writes on, with N of its columns written (0, 3, 6, 9, 12): the end of the preface and its closing poem, top to bottom and right to left

The film's words are not pictures: they are pbrt_scenes/film_assets/tengwang/captions.txt, set in a font when the video is made (src/shared/cartoon/caption_file.h).
The preface (AD 675) is in the public domain. Needs Pillow and the Windows font SimSun (simsun.ttc).
Run from the repository root:  python scripts/make_tengwang_assets.py
"""

import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "pbrt_scenes", "film_assets", "tengwang")
FONTS = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")


# The manuscript: the last part of the preface and its poem, one column per entry (each column top to bottom, the columns from right to left).
MANUSCRIPT = [
    "落霞与孤鹜齐飞，秋水共长天一色。",
    "渔舟唱晚，响穷彭蠡之滨；",
    "雁阵惊寒，声断衡阳之浦。",
    "天高地迥，觉宇宙之无穷；",
    "兴尽悲来，识盈虚之有数。",
    "老当益壮，宁移白首之心？",
    "穷且益坚，不坠青云之志。",
    "滕王高阁临江渚，佩玉鸣鸾罢歌舞。",
    "画栋朝飞南浦云，珠帘暮卷西山雨。",
    "闲云潭影日悠悠，物换星移几度秋。",
    "阁中帝子今何在？",
    "槛外长江空自流。",
]


def font(name, size, index=0):
    return ImageFont.truetype(os.path.join(FONTS, name), size, index=index)


def manuscript(columns_written, seed=7):
    """Old paper ruled in faint vermilion columns, with `columns_written` columns of ink written from the right, unpunctuated as a manuscript of the time is."""
    W, H = 2048, 1365
    rnd = random.Random(seed)
    paper = Image.new("RGB", (W, H), (236, 226, 202))
    noise = Image.effect_noise((W // 4, H // 4), 22).resize((W, H), Image.BICUBIC)
    fib = Image.effect_noise((W, H), 30).filter(ImageFilter.GaussianBlur(0.8))
    paper = Image.blend(paper, Image.merge("RGB", [noise.point(lambda v: 218 + v // 12)] * 3), 0.30)
    paper = Image.blend(paper, Image.merge("RGB", [fib.point(lambda v: 225 + v // 16)] * 3), 0.15)
    tone = Image.new("RGB", (W, H), (224, 204, 166))
    vign = Image.radial_gradient("L").resize((W, H)).point(lambda v: int(v * 0.5))
    paper = Image.composite(tone, paper, vign)
    size = 74
    col_w = 118
    top, bottom = 150, H - 130
    rules = ImageDraw.Draw(paper)
    x_right = W - 170
    n_cols = int((x_right - 150) // col_w)
    for k in range(n_cols + 1):
        x = x_right - k * col_w
        rules.line([(x, top - 30), (x, bottom + 30)], fill=(196, 120, 96), width=2)
    rules.rectangle([x_right - n_cols * col_w, top - 30, x_right, bottom + 30], outline=(186, 104, 82), width=4)
    f = font("simsun.ttc", size)
    for i, column in enumerate(MANUSCRIPT[:columns_written]):
        if i >= n_cols:
            break
        text = "".join(ch for ch in column if ch not in "，。；？！：")
        cx = x_right - (i + 0.5) * col_w
        step = min(size * 1.12, (bottom - top) / max(1, len(text)))
        y = top
        for ch in text:
            # each character its own ink: a fresh dip is darker and spreads more
            load = rnd.uniform(0.78, 1.0)
            glyph = Image.new("L", (size * 2, size * 2), 0)
            gd = ImageDraw.Draw(glyph)
            w = gd.textlength(ch, font=f)
            gd.text((size - w / 2, size / 2), ch, font=f, fill=255, stroke_width=1, stroke_fill=255)
            glyph = glyph.rotate(rnd.uniform(-2.0, 2.0), resample=Image.BICUBIC, center=(size, size))
            bleed = glyph.filter(ImageFilter.GaussianBlur(1.6 + 0.8 * load)).point(lambda v: min(255, int(v * 1.25)))
            core = glyph.filter(ImageFilter.GaussianBlur(0.5))
            alpha = Image.eval(Image.blend(bleed, core, 0.55), lambda v: int(v * load))
            ink = Image.new("RGB", glyph.size, (int(22 * (1.2 - load)), int(16 * (1.2 - load)), int(14 * (1.2 - load))))
            paper.paste(ink, (int(cx - size + rnd.uniform(-2, 2)), int(y - size / 2 + rnd.uniform(-1.5, 1.5))), alpha)
            y += step
    return paper


def plaque():
    """The Pavilion's name board: gold characters on deep blue in a gold frame, read from the right as a board of the time is."""
    W, H = 1536, 512
    board = Image.new("RGB", (W, H), (16, 34, 74))
    d = ImageDraw.Draw(board)
    for k, (w, c) in enumerate([(26, (190, 146, 52)), (10, (120, 84, 26)), (6, (226, 188, 92))]):
        inset = 6 + sum(x for x, _ in [(26, 0), (10, 0), (6, 0)][:k])
        d.rectangle([inset, inset, W - 1 - inset, H - 1 - inset], outline=c, width=w)
    f = font("simsun.ttc", 300)
    for i, ch in enumerate("滕王阁"):   # 滕王阁, the first at the right
        cx = W - (i + 0.5) * (W - 140) / 3 - 70
        w = d.textlength(ch, font=f)
        d.text((cx - w / 2 + 4, 92 + 4), ch, font=f, fill=(70, 46, 10), stroke_width=4, stroke_fill=(70, 46, 10))
        d.text((cx - w / 2, 92), ch, font=f, fill=(232, 192, 90), stroke_width=4, stroke_fill=(232, 192, 90))
    noise = Image.effect_noise((W, H), 18).convert("RGB")
    return Image.blend(board, Image.merge("RGB", [noise.split()[0]] * 3), 0.05)


def banner():
    """The general's war banner: a great black character (Li, his name) in a white disc on crimson silk with a scalloped border."""
    W, H = 768, 1152
    img = Image.new("RGB", (W, H), (150, 14, 12))
    d = ImageDraw.Draw(img)
    d.rectangle([24, 24, W - 25, H - 25], outline=(212, 168, 60), width=14)
    d.ellipse([W / 2 - 250, H / 2 - 300, W / 2 + 250, H / 2 + 200], fill=(236, 226, 206))
    f = font("simsun.ttc", 380)
    ch = "李"   # Li
    w = d.textlength(ch, font=f)
    d.text((W / 2 - w / 2, H / 2 - 300 + 40), ch, font=f, fill=(16, 12, 10), stroke_width=8, stroke_fill=(16, 12, 10))
    noise = Image.effect_noise((W, H), 20).convert("RGB")
    return Image.blend(img, Image.merge("RGB", [noise.split()[0]] * 3), 0.06)


def main():
    os.makedirs(OUT, exist_ok=True)
    for n in (0, 3, 6, 9, 12):
        manuscript(n).save(os.path.join(OUT, "manuscript_%d.jpg" % n), quality=90)
    plaque().save(os.path.join(OUT, "plaque.jpg"), quality=92)
    banner().save(os.path.join(OUT, "banner_li.jpg"), quality=90)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
