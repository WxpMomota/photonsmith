"""make_tengwang_assets.py -- the pictures "Preface to the Pavilion of Prince Teng" (src/shared/cartoon/tengwang*.h) brings with it, in pbrt_scenes/film_assets/tengwang:

  captions/cNN.png    each line of Wang Bo's preface the film shows, in Chinese with the English beneath, white on transparent with a soft shadow, all on the same canvas width
                      (so the film lays every one at the same size: cartoon::Caption width 0.625)
  captions/tNN.png    the end title: the preface's name, the author and the year, and the line the film ends on
  plaque.jpg          the Pavilion's name board, gold on blue
  banner_li.jpg       the general Li Guang's banner
  manuscript_N.jpg    the paper Wang Bo writes on, with N of its columns written (0, 3, 6, 9, 12): the end of the preface and its closing poem, top to bottom and right to left

The preface (AD 675) is in the public domain; the English lines are the film's own. Needs Pillow and the Windows fonts SimSun (simsun.ttc) and Palatino Linotype (pala*.ttf).
Run from the repository root:  python scripts/make_tengwang_assets.py
"""

import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "pbrt_scenes", "film_assets", "tengwang")
FONTS = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")

CANVAS_W = 2400          # every caption is this wide (2x the 1200 px it is laid at on a 1920-wide frame)
ZH_SIZE, EN_SIZE = 92, 52
ZH_COLOUR = (255, 246, 226)
EN_COLOUR = (236, 228, 212)

CAPTIONS = [
    ("c01", "豫章故郡，洪都新府。", "The ancient commandery of Yuzhang is now called Hongzhou."),
    ("c02", "星分翼轸，地接衡庐。", "Its stars correspond to the constellations Yi and Zhen; its territory connects Mount Heng and Mount Lu."),
    ("c03", "千里逢迎，高朋满座。", "Distinguished guests fill the hall; honored friends arrive from a thousand li away."),
    ("c04", "披绣闼，俯雕甍，山原旷其盈视，川泽纡其骇瞩。",
     "Opening the embroidered gates and looking down from the carved roofs, the mountains spread endlessly before the eye, while the rivers and plains astonish the beholder."),
    ("c05", "渔舟唱晚，响穷彭蠡之滨；", "Fishing boats sing in the evening, their voices reaching the shores of Lake Pengli."),
    ("c06", "雁阵惊寒，声断衡阳之浦。", "Wild geese, chilled by the autumn air, cry out beneath the heights of Mount Heng."),
    ("c07", "落霞与孤鹜齐飞，", "The setting clouds and the solitary bird fly together."),
    ("c08", "秋水共长天一色。", "The autumn waters merge with the endless sky into a single hue."),
    ("c09", "天高地迥，觉宇宙之无穷；", "Heaven is high, the earth vast; one feels the infinity of the universe."),
    ("c10", "兴尽悲来，识盈虚之有数。", "Joy exhausted, sorrow follows."),
    ("c11", "关山难越，谁悲失路之人？", "The mountain passes are difficult to cross. Who will pity the traveler who has lost his way?"),
    ("c12", "萍水相逢，尽是他乡之客。", "Meeting by chance, all are guests from distant lands."),
    ("c13", "嗟乎！冯唐易老，", "Alas, Feng Tang grew old\u2026"),
    ("c14", "李广难封。", "\u2026and Li Guang found promotion difficult."),
    ("c15", "所赖君子见机，达人知命。", "Yet though fate may be unfavorable, a noble man does not abandon his aspirations."),
    ("c16", "老当益壮，宁移白首之心？", "Though old, one\u2019s ambition should remain strong."),
    ("c17", "穷且益坚，不坠青云之志。", "Though poor and frustrated, one\u2019s resolve should become even firmer."),
    ("c18", "滕王高阁临江渚，", "The Pavilion of Prince Teng stands high above the riverbank\u2026"),
]

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


def wrap(draw, text, fnt, width):
    """Splits English text into lines no wider than `width`."""
    words, lines, line = text.split(" "), [], ""
    for w in words:
        trial = (line + " " + w).strip()
        if draw.textlength(trial, font=fnt) <= width or not line:
            line = trial
        else:
            lines.append(line)
            line = w
    if line:
        lines.append(line)
    return lines


def with_shadow(layer, radius=7, strength=0.62):
    """The text layer over a soft dark halo of itself (the words stay legible over a bright sky)."""
    alpha = layer.split()[3]
    halo = alpha.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(radius))
    halo = halo.point(lambda v: int(v * strength))
    shadow = Image.new("RGBA", layer.size, (8, 6, 4, 0))
    shadow.putalpha(halo)
    return Image.alpha_composite(shadow, layer)


def caption(zh, en, zh_size=ZH_SIZE, en_size=EN_SIZE, gap=26):
    zh_font, en_font = font("simsun.ttc", zh_size), font("palai.ttf", en_size)
    probe = ImageDraw.Draw(Image.new("RGBA", (10, 10)))
    en_lines = wrap(probe, en, en_font, CANVAS_W - 160) if en else []
    pad = 40
    zh_h = zh_size + 16 if zh else 0
    en_h = len(en_lines) * int(en_size * 1.32)
    h = pad * 2 + zh_h + (gap if zh and en_lines else 0) + en_h
    layer = Image.new("RGBA", (CANVAS_W, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    y = pad
    if zh:
        w = d.textlength(zh, font=zh_font)
        # (SimSun is thin: a hair of stroke gives it the weight of a brush at this size)
        d.text(((CANVAS_W - w) / 2, y), zh, font=zh_font, fill=ZH_COLOUR + (255,), stroke_width=1, stroke_fill=ZH_COLOUR + (255,))
        y += zh_h + gap
    for line in en_lines:
        w = d.textlength(line, font=en_font)
        d.text(((CANVAS_W - w) / 2, y), line, font=en_font, fill=EN_COLOUR + (255,))
        y += int(en_size * 1.32)
    return with_shadow(layer)


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
    os.makedirs(os.path.join(OUT, "captions"), exist_ok=True)
    for key, zh, en in CAPTIONS:
        caption(zh, en).save(os.path.join(OUT, "captions", key + ".png"), optimize=True)
    # the end title: the name large, the author and the year, the last line
    caption("《滕王阁序》", "", zh_size=150).save(os.path.join(OUT, "captions", "t01.png"), optimize=True)
    caption("王勃", "WANG BO \u00b7 675 AD", zh_size=70, en_size=48).save(os.path.join(OUT, "captions", "t02.png"), optimize=True)
    caption("穷且益坚，不坠青云之志。", "\u201cThough poor and frustrated, one\u2019s resolve should become even firmer, never allowing one\u2019s ambition to fall from the clouds.\u201d",
            zh_size=78, en_size=46).save(os.path.join(OUT, "captions", "t03.png"), optimize=True)
    for n in (0, 3, 6, 9, 12):
        manuscript(n).save(os.path.join(OUT, "manuscript_%d.jpg" % n), quality=90)
    plaque().save(os.path.join(OUT, "plaque.jpg"), quality=92)
    banner().save(os.path.join(OUT, "banner_li.jpg"), quality=90)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
