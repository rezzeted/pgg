#!/usr/bin/env python3
"""Crop a PggViewer preview PNG to a square around the mesh silhouette.

Clear color matches GeometryPreview::kClearColor (0.14, 0.15, 0.18).
Called by tools/pgg/regen_gallery.sh after each --shot.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops

# GeometryPreview::kClearColor → 8-bit.
CLEAR = (36, 38, 46)
TOLERANCE = 12
PAD_FRAC = 0.10  # padding around the silhouette, fraction of the longer bbox side


def silhouette_bbox(im: Image.Image) -> tuple[int, int, int, int] | None:
    rgb = im.convert("RGB")
    diff = ImageChops.difference(rgb, Image.new("RGB", rgb.size, CLEAR))
    r, g, b = diff.split()
    mag = ImageChops.lighter(ImageChops.lighter(r, g), b)
    mask = mag.point(lambda p: 255 if p > TOLERANCE else 0)
    return mask.getbbox()


def square_crop(im: Image.Image, bbox: tuple[int, int, int, int], pad_frac: float) -> Image.Image:
    x0, y0, x1, y1 = bbox  # PIL getbbox: exclusive x1/y1
    bw = x1 - x0
    bh = y1 - y0
    cx = (x0 + x1) * 0.5
    cy = (y0 + y1) * 0.5
    pad = pad_frac * max(bw, bh)
    side = int(round(max(bw + 2.0 * pad, bh + 2.0 * pad)))
    img_w, img_h = im.size
    side = min(side, img_w, img_h)
    left = int(round(cx - side * 0.5))
    top = int(round(cy - side * 0.5))
    left = max(0, min(left, img_w - side))
    top = max(0, min(top, img_h - side))
    return im.crop((left, top, left + side, top + side))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("png", type=Path)
    ap.add_argument("--pad", type=float, default=PAD_FRAC)
    args = ap.parse_args()
    path: Path = args.png
    if not path.is_file():
        print(f"crop_gallery: not found: {path}", file=sys.stderr)
        return 1
    im = Image.open(path)
    bbox = silhouette_bbox(im)
    if bbox is None:
        print(f"crop_gallery: empty silhouette in {path}", file=sys.stderr)
        return 1
    out = square_crop(im, bbox, args.pad)
    out.save(path, format="PNG", optimize=True)
    print(f"crop_gallery: {path.name}  {im.size[0]}x{im.size[1]} -> {out.size[0]}x{out.size[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
