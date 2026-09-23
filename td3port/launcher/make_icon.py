"""Writes app.ico: the banded road drawn in icon.cpp, for Explorer. Needs Pillow.

    python make_icon.py
"""
from pathlib import Path

from PIL import Image

tile = Image.new("RGBA", (16, 16))


def put(x, y, rgb):
    tile.putpixel((x, y), (rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF, 255))


def dark(y):  # the bands: two rows each near the bottom, one row towards the horizon
    return ((y - 12) // 2) % 2 == 1 if y >= 12 else y % 2 == 1


for y in range(16):
    for x in range(16):
        put(x, y, 0x4A7FD0 if y < 6 else 0x1E6E1E if dark(y) else 0x2A8A2A)
for y in range(6, 16):  # the road: two pixels wide at the horizon, the whole width at the bottom
    half = 1 + (y - 6) * 7 // 9
    for x in range(8 - half, 8 + half):
        put(x, y, 0xC8C8C8 if x in (8 - half, 7 + half) else 0x2E2E2E if dark(y) else 0x3A3A3A)
for y in (8, 11, 12, 15):  # the centre line's dashes, longer nearer
    for x in (7, 8):
        put(x, y, 0xFFFF55)

sizes = [16, 20, 24, 32, 48, 64, 256]
big = tile.resize((256, 256), Image.NEAREST)
big.save(Path(__file__).with_name("app.ico"), sizes=[(s, s) for s in sizes])
