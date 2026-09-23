"""Contact sheet of td3port snapshots (TD3_SNAPSHOT_DIR): snapNNNN.bmp -> <dir>/sheet.png.

usage: snapsheet.py DIR [COLUMNS] [SCALE]
"""
import glob, os, sys
from PIL import Image, ImageDraw

d = sys.argv[1]
cols = int(sys.argv[2]) if len(sys.argv) > 2 else 4
scale = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
files = sorted(glob.glob(os.path.join(d, 'snap*.bmp')))
w, h = int(320 * scale), int(200 * scale)
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (w + 6), max(1, rows) * (h + 18)), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
for k, f in enumerate(files):
    x, y = (k % cols) * (w + 6), (k // cols) * (h + 18)
    sheet.paste(Image.open(f).convert('RGB').resize((w, h), Image.NEAREST), (x, y + 14))
    dr.text((x + 2, y + 1), os.path.basename(f), fill=(255, 255, 0))
sheet.save(os.path.join(d, 'sheet.png'))
print('%d snapshots -> %s' % (len(files), os.path.join(d, 'sheet.png')))
