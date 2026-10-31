"""Gallery image for the README: the three bundled skins rendered with LIVE data, on a soft backdrop."""
import sys
from PIL import Image
a = Image.open("docs/img/skin_aurora.png").convert("RGBA")
g = Image.open("docs/img/skin_glass.png").convert("RGBA")
m = Image.open("docs/img/skin_minimal.png").convert("RGBA")
pad = 70
W = a.width + max(g.width, m.width) + pad * 3
H = max(a.height, g.height + m.height + pad) + pad * 2
bg = Image.new("RGBA", (W, H))
px = bg.load()
for y in range(H):
    for x in range(W):
        t = (x / W * 0.6 + y / H * 0.4)
        px[x, y] = (int(34 + 70 * t), int(44 + 40 * (1 - t)), int(86 + 90 * t), 255)
bg.alpha_composite(a, (pad, pad))
bg.alpha_composite(g, (pad * 2 + a.width, pad))
bg.alpha_composite(m, (pad * 2 + a.width + (max(g.width, m.width) - m.width) // 2, pad * 2 + g.height))
bg.convert("RGB").save(sys.argv[1])
print(bg.size)
