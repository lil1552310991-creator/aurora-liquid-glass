"""裁剪 / 放大 / 对比截图的小工具（真机验收用）。

用法:
  python tools/crop_shot.py crop  <in.png> <out.png> <x> <y> <w> <h> [scale]
  python tools/crop_shot.py diff  <a.png> <b.png> [x y w h]
  python tools/crop_shot.py edges <in.png> [--col x] [--row y]
"""
import sys

from PIL import Image, ImageChops


def crop(args):
    src, dst, x, y, w, h = args[0], args[1], *map(int, args[2:6])
    scale = int(args[6]) if len(args) > 6 else 1
    im = Image.open(src).convert("RGB").crop((x, y, x + w, y + h))
    if scale > 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(dst)
    print(f"{dst}: {im.width}x{im.height}")


def diff(args):
    a = Image.open(args[0]).convert("RGB")
    b = Image.open(args[1]).convert("RGB")
    if len(args) >= 6:
        x, y, w, h = map(int, args[2:6])
        a = a.crop((x, y, x + w, y + h))
        b = b.crop((x, y, x + w, y + h))
    if a.size != b.size:
        print(f"size mismatch {a.size} vs {b.size}")
        return
    d = ImageChops.difference(a, b)
    bbox = d.getbbox()
    hist = d.convert("L").histogram()
    changed = sum(hist[8:])
    total = a.width * a.height
    print(f"差异像素(>=8) {changed}/{total} = {100.0*changed/total:.2f}%  包围盒={bbox}")
    if bbox:
        sub = d.crop(bbox)
        print(f"  最大通道差 = {max(sub.getextrema(), key=lambda e: e[1])[1]}")


def edges(args):
    im = Image.open(args[0]).convert("RGB")
    if "--col" in args:
        x = int(args[args.index("--col") + 1])
        col = [im.getpixel((x, y)) for y in range(im.height)]
        print(f"列 x={x} 亮度剖面（每 40px 采样）:")
        for y in range(0, im.height, 40):
            r, g, b = col[y]
            print(f"  y={y:5d}  {(r+g+b)//3:3d}")
    if "--row" in args:
        y = int(args[args.index("--row") + 1])
        print(f"行 y={y} 亮度剖面（每 20px 采样）:")
        for x in range(0, im.width, 20):
            r, g, b = im.getpixel((x, y))
            print(f"  x={x:5d}  {(r+g+b)//3:3d}")


if __name__ == "__main__":
    mode = sys.argv[1]
    rest = sys.argv[2:]
    {"crop": crop, "diff": diff, "edges": edges}[mode](rest)
