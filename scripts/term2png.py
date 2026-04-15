#!/usr/bin/env python3
"""Render terminal text (with basic ANSI colour support) to a PNG.
Reads stdin (or a file), writes a PNG to the output path.

usage: term2png.py [--title TITLE] [--font-size N] <out.png>
"""
import argparse
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ANSI_RE = re.compile(r"\x1b\[([0-9;]*)m")

# VSCode-ish dark palette
BG        = (30, 31, 40)
FG        = (221, 226, 239)
TITLE_BG  = (40, 42, 54)
PROMPT_FG = (139, 233, 253)

ANSI16 = {
    30: (75, 75, 75),   31: (255, 85, 85),   32: (80, 250, 123),
    33: (241, 250, 140), 34: (98, 114, 164), 35: (255, 121, 198),
    36: (139, 233, 253), 37: (221, 226, 239),
    90: (98, 114, 164),  91: (255, 110, 110), 92: (120, 255, 150),
    93: (255, 255, 140), 94: (120, 140, 200), 95: (255, 140, 220),
    96: (160, 240, 255), 97: (255, 255, 255),
}

def tokenize(text):
    """Yield (fg, bold, segment) tuples."""
    cur_fg = FG
    cur_bold = False
    i = 0
    while i < len(text):
        m = ANSI_RE.search(text, i)
        if not m:
            yield (cur_fg, cur_bold, text[i:])
            return
        if m.start() > i:
            yield (cur_fg, cur_bold, text[i:m.start()])
        codes = [int(c) if c else 0 for c in m.group(1).split(";")]
        for c in codes:
            if c == 0:
                cur_fg = FG; cur_bold = False
            elif c == 1:
                cur_bold = True
            elif c == 22:
                cur_bold = False
            elif c == 39:
                cur_fg = FG
            elif c in ANSI16:
                cur_fg = ANSI16[c]
        i = m.end()

def render(text, out_path, title=None, font_size=16, padding=16):
    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/hack/Hack-Regular.ttf", font_size)
        bold = ImageFont.truetype(
            "/usr/share/fonts/truetype/hack/Hack-Bold.ttf", font_size)
    except OSError:
        font = ImageFont.load_default()
        bold = font

    lines = text.rstrip("\n").split("\n")
    # measure
    tmp = Image.new("RGB", (10, 10))
    d = ImageDraw.Draw(tmp)
    char_w = d.textlength("M", font=font)
    line_h = font_size + 6
    max_cols = max((len(ANSI_RE.sub("", ln)) for ln in lines), default=0)
    title_h = line_h + 8 if title else 0

    img_w = int(padding * 2 + max_cols * char_w)
    img_w = max(img_w, 600)
    img_h = int(padding * 2 + len(lines) * line_h + title_h)

    img = Image.new("RGB", (img_w, img_h), BG)
    draw = ImageDraw.Draw(img)

    y = padding
    if title:
        draw.rectangle([0, 0, img_w, title_h + padding // 2],
                       fill=TITLE_BG)
        # window "traffic lights"
        cx = 14
        for col in [(255, 95, 86), (255, 189, 46), (39, 201, 63)]:
            draw.ellipse([cx - 6, 8, cx + 6, 20], fill=col)
            cx += 18
        draw.text((cx + 8, 6), title, fill=FG, font=font)
        y = title_h + padding // 2 + 4

    for ln in lines:
        x = padding
        for fg, is_bold, seg in tokenize(ln):
            if not seg:
                continue
            draw.text((x, y), seg, fill=fg, font=(bold if is_bold else font))
            x += int(d.textlength(seg, font=(bold if is_bold else font)))
        y += line_h

    img.save(out_path)
    return img_w, img_h

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--title", default=None)
    ap.add_argument("--input", default="-")
    ap.add_argument("--font-size", type=int, default=15)
    args = ap.parse_args()

    if args.input == "-":
        text = sys.stdin.read()
    else:
        text = Path(args.input).read_text()

    # Collapse \r so progress-bar updates show only the final state per line.
    collapsed_lines = []
    for raw in text.split("\n"):
        if "\r" in raw:
            raw = raw.split("\r")[-1]
        collapsed_lines.append(raw)
    text = "\n".join(collapsed_lines)

    w, h = render(text, args.out, title=args.title, font_size=args.font_size)
    print(f"{args.out}: {w}x{h}")

if __name__ == "__main__":
    main()
