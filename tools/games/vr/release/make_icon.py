#!/usr/bin/env python3
"""The app icon: "VR" in chiselled low-poly letters on a faceted tile over a chequered-flag band,
drawn from scratch (no art from the game), in the style of VF's (tools/games/vf/release/make_icon.py).
Writes assets/games/vr/icon/icon.png (1024x1024, the macOS icon grid: an 824px rounded tile with room
for the shadow), VR.icns and VR.ico; with variant names, icon_<variant>.png and choices.png instead.

    tools/games/vr/release/make_icon.py [variant ...]      variants: red, blue, black
"""
import math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter

SS = 4                     # supersampling
N = 1024 * SS
OUT = os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', 'assets', 'games', 'vr', 'icon')
DEFAULT = 'red'

PALETTES = {
    # tile (two corners), letter base colour, letter edge colour
    'red':   ((215, 25, 30), (110, 0, 20), (250, 250, 255), (60, 80, 160)),
    'blue':  ((20, 70, 210), (8, 20, 90), (250, 250, 255), (200, 20, 30)),
    'black': ((40, 42, 52), (8, 8, 14), (235, 30, 35), (110, 0, 15)),
}

def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)

def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def _skew(p, s=0.22):       # italic, leaning forward
    x, y = p
    return (x + (512 - y) * s, y)

_FIT = None
def skew(p):
    """Slanted, then scaled and centred to sit inside the tile with a margin (above the flag band)."""
    global _FIT
    if _FIT is None:
        pts = [_skew(q) for st in strokes() for q in st]
        x0 = min(x for x, _ in pts); x1 = max(x for x, _ in pts)
        y0 = min(y for _, y in pts); y1 = max(y for _, y in pts)
        k = min(600 / (x1 - x0), 400 / (y1 - y0))
        _FIT = (k, 512 - (x0 + x1) / 2 * k, 455 - (y0 + y1) / 2 * k)
    k, ox, oy = _FIT
    x, y = _skew(p)
    return (x * k + ox, y * k + oy)

def strokes():
    """The letters as quads (in a 1024 design space): each stroke is chiselled separately."""
    q = []
    # V: two arms meeting at the bottom
    q.append([(120, 300), (215, 300), (300, 640), (250, 740)])
    q.append([(380, 300), (475, 300), (350, 740), (250, 740)])
    # R: stem, the bowl (top, side, bottom), the leg
    q.append([(520, 300), (605, 300), (605, 740), (520, 740)])
    q.append([(605, 300), (790, 300), (760, 375), (605, 375)])
    q.append([(790, 300), (860, 370), (845, 480), (760, 375)])
    q.append([(605, 480), (845, 480), (780, 550), (605, 550)])
    q.append([(680, 550), (780, 550), (900, 740), (800, 740)])
    return q

def chisel(d, quad, base, edge, light):
    """A bevelled block: the top face (two triangles) and four sloped edges, each flat-shaded."""
    cx = sum(p[0] for p in quad) / 4
    cy = sum(p[1] for p in quad) / 4
    inner = [(cx + (x - cx) * 0.62, cy + (y - cy) * 0.62) for x, y in quad]
    faces = []
    for i in range(4):
        a, b = quad[i], quad[(i + 1) % 4]
        ia, ib = inner[i], inner[(i + 1) % 4]
        # the facet's outward direction decides its brightness
        nx, ny = (b[1] - a[1]), -(b[0] - a[0])
        ln = math.hypot(nx, ny) or 1
        lit = (nx * light[0] + ny * light[1]) / ln
        faces.append(([a, b, ib, ia], mix(shade(edge, 0.55), shade(base, 1.15), (lit + 1) / 2)))
    faces.append(([inner[0], inner[1], inner[2]], shade(base, 1.0)))
    faces.append(([inner[0], inner[2], inner[3]], shade(base, 0.9)))
    for poly, col in faces:
        d.polygon([(skew(p)[0] * SS, skew(p)[1] * SS) for p in poly], fill=col)

def tile(img, c1, c2):
    """A rounded tile of flat-shaded triangles, darker toward one corner, with a chequered band."""
    mask = Image.new('L', (N, N), 0)
    m = 100 * SS
    ImageDraw.Draw(mask).rounded_rectangle((m, m, N - m, N - m), radius=185 * SS, fill=255)
    layer = Image.new('RGB', (N, N))
    d = ImageDraw.Draw(layer)
    rnd = random.Random(7)
    step = 128 * SS
    pts = {}
    for gy in range(0, N + step, step):
        for gx in range(0, N + step, step):
            j = 0 if gx in (0, N) or gy in (0, N) else step * 0.32
            pts[gx, gy] = (gx + rnd.uniform(-j, j), gy + rnd.uniform(-j, j))
    for gy in range(0, N, step):
        for gx in range(0, N, step):
            a, b, c, e = pts[gx, gy], pts[gx + step, gy], pts[gx + step, gy + step], pts[gx, gy + step]
            for tri in ((a, b, c), (a, c, e)):
                cx = sum(p[0] for p in tri) / 3 / N
                cy = sum(p[1] for p in tri) / 3 / N
                col = mix(c1, c2, min(1, max(0, (cx + cy) / 2 + rnd.uniform(-0.08, 0.08))))
                d.polygon(tri, fill=shade(col, rnd.uniform(0.88, 1.12)))
    # the chequered flag: two rows of squares across the bottom, slanted as the letters
    sq, top = 56, 700
    for row in range(2):
        for col in range(-2, 20):
            if (row + col) % 2: continue
            x0, y0 = 100 + col * sq, top + row * sq
            quad = [(x0, y0), (x0 + sq, y0), (x0 + sq, y0 + sq), (x0, y0 + sq)]
            d.polygon([((x + (512 - y) * 0.22) * SS, y * SS) for x, y in quad], fill=(245, 245, 250))
        for col in range(-2, 20):
            if not (row + col) % 2: continue
            x0, y0 = 100 + col * sq, top + row * sq
            quad = [(x0, y0), (x0 + sq, y0), (x0 + sq, y0 + sq), (x0, y0 + sq)]
            d.polygon([((x + (512 - y) * 0.22) * SS, y * SS) for x, y in quad], fill=(20, 20, 26))
    # a soft drop shadow under the tile
    shadow = Image.new('L', (N, N), 0)
    ImageDraw.Draw(shadow).rounded_rectangle((m, m + 14 * SS, N - m, N - m + 14 * SS), radius=185 * SS, fill=110)
    shadow = shadow.filter(ImageFilter.GaussianBlur(18 * SS))
    img.paste((0, 0, 0, 255), (0, 0), shadow)
    img.paste(layer, (0, 0), mask)

def make(name):
    c1, c2, base, edge = PALETTES[name]
    img = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    tile(img, c1, c2)
    # the letters, with a dark outline (the same shapes, grown) for contrast on the tile
    d = ImageDraw.Draw(img)
    for q in strokes():
        cx = sum(p[0] for p in q) / 4; cy = sum(p[1] for p in q) / 4
        grown = [(x + (12 if x > cx else -12), y + (12 if y > cy else -12)) for x, y in q]
        d.polygon([(skew(p)[0] * SS, (skew(p)[1] + 10) * SS) for p in grown], fill=(0, 0, 0, 170))
    for q in strokes():
        chisel(d, q, base, edge, (-0.55, -0.83))
    return img.resize((1024, 1024), Image.LANCZOS)

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    if len(sys.argv) > 1:
        ims = [make(n) for n in sys.argv[1:]]
        for n, im in zip(sys.argv[1:], ims): im.save(os.path.join(OUT, f'icon_{n}.png'))
        sheet = Image.new('RGB', (360 * len(ims) + 20, 400), (236, 236, 240))
        for i, im in enumerate(ims):
            small = im.resize((340, 340), Image.LANCZOS)
            sheet.paste(small, (20 + i * 360, 30), small)
        sheet.save(os.path.join(OUT, 'choices.png'))
    else:
        im = make(DEFAULT)
        im.save(os.path.join(OUT, 'icon.png'))
        im.save(os.path.join(OUT, 'VR.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
        im.save(os.path.join(OUT, 'VR.icns'))
