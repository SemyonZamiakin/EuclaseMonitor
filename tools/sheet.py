"""Preview sheet: pastes transparent skin PNGs onto a dark desktop-like backdrop (what you would see over a game)."""
import sys
from PIL import Image
paths = sys.argv[2:]
imgs = [Image.open(p).convert("RGBA") for p in paths]
W = max(i.width for i in imgs) + 120
H = sum(i.height for i in imgs) + 60 * (len(imgs) + 1)
bg = Image.new("RGBA", (W, H), (24, 52, 62, 255))
for y in range(H):  # a soft gradient so translucent backgrounds are visible
    for x in range(0, W, 8):
        t = (x / W + y / H) / 2
        c = (int(22 + 46 * t), int(60 + 70 * t), int(72 + 70 * t), 255)
        bg.paste(Image.new("RGBA", (8, 1), c), (x, y))
y = 60
for im in imgs:
    bg.alpha_composite(im, ((W - im.width) // 2, y)); y += im.height + 60
bg.convert("RGB").save(sys.argv[1])
