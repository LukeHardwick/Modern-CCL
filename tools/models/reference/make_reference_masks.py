#!/usr/bin/env python3
# Derive the F1 reference silhouettes from the public-domain three-view drawing (task T22e).
#
#   python3 tools/models/reference/make_reference_masks.py            (from the repo root)
#
# Rasterises cessna_172_skyhawk_line_drawing.svg with rsvg-convert at a fixed scale (the exact
# command is printed and stored in cessna_172_reference.toml), cuts the three views, and turns
# each outline into a filled silhouette: strokes are black on white; gaps up to 2 * CLOSE px
# are bridged; everything the outside cannot reach is inside; the result is eroded by
# HALF + CLOSE so the mask edge lies on the CENTRE of the drawn outline. Lines that do not
# enclose anything (dimension lines, the dashed ground line) vanish. Output (committed, so the
# gate never needs an SVG renderer): cessna_172_{front,side,top}_mask.png (0 / 255) and
# cessna_172_reference.toml (provenance + landmarks in mask pixels). Same input and same
# rsvg-convert version give the same bytes.

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.dont_write_bytecode = True

import png_io  # noqa: E402
import raster_util as ru  # noqa: E402

SVG = HERE / "cessna_172_skyhawk_line_drawing.svg"
SCALE = 2           # rsvg-convert zoom: 1800 x 1200 -> 3600 x 2400 px
CLOSE_1X = 2.5      # drawing px: strokes are dilated by this to bridge pen gaps (5 px at scale 2)
HALF_1X = 1.5       # drawing px: half the 3 px outline stroke, so the edge is at the line centre
THRESH = 128
MIN_STROKE_1X = 15  # dark components with fewer drawing px^2 (dashes of the ground line) are dropped
# crop boxes in 1x drawing pixels (x0, y0, x1, y1); the dashed ground lines are inside them
VIEWS = {"front": (0, 0, 1050, 380), "side": (1030, 0, 1800, 380), "top": (0, 450, 1050, 1200)}
# Regions the drawing closes with lines but that are open sky on the aircraft: in the front
# view the triangle between each wing strut, the wing underside and the fuselage side ; and the two
# slot outlines drawn inside them, which are erased (boxes in 1x pixels) before filling. A seed
# (1x drawing pixel) inside such a region marks it as outside the silhouette.
ERASE = {"front": [(372, 175, 463, 193), (573, 175, 662, 191)], "side": [], "top": []}
SEE_THROUGH = {"front": [(440, 215), (597, 215)], "side": [], "top": []}


def render(tmp, scale=SCALE):
    png = Path(tmp) / "drawing.png"
    cmd = ["rsvg-convert", "-z", str(scale), "-b", "white", str(SVG), "-o", str(png)]
    subprocess.run(cmd, check=True)
    cmd = ["rsvg-convert", "-z", str(scale), "-b", "white", SVG.name, "-o", "drawing.png"]
    ver = subprocess.run(["rsvg-convert", "--version"], check=True, capture_output=True,
                         text=True).stdout.splitlines()[0].strip()
    arr, _ = png_io.read_png(png.read_bytes())
    return arr, " ".join(cmd), ver


def silhouette(dark, scale, close, half, min_stroke, seeds=(), erase=()):
    dark = dark.copy()
    # specks and dashes (the dotted ground line) are not part of any outline
    lab, n = ru.label_components(dark, 8)
    if n:
        sizes = np.bincount(lab.ravel(), minlength=n + 1)
        dark[np.isin(lab, np.nonzero(sizes < min_stroke)[0]) & (lab > 0)] = False
    for ex0, ey0, ex1, ey1 in erase:
        dark[ey0 * scale:ey1 * scale, ex0 * scale:ex1 * scale] = False
    closed = ru.dilate(dark, close)
    # outside = white (after closing) pixels connected to the crop border
    lab, _n = ru.label_components(~closed, 4)
    border = np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))
    border = np.concatenate([border] + [[lab[y * scale, x * scale]] for x, y in seeds])
    outside = np.isin(lab, border[border > 0])
    inside = ~outside
    return ru.erode(inside, half + close)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build(out_dir, scale=SCALE, close_1x=CLOSE_1X, half_1x=HALF_1X, verbose=True):
    """Write the three masks and cessna_172_reference.toml into out_dir."""
    out_dir = Path(out_dir)
    close, half = int(round(close_1x * scale)), int(round(half_1x * scale))
    min_stroke = int(round(MIN_STROKE_1X * scale * scale))
    with tempfile.TemporaryDirectory() as tmp:
        arr, cmd, ver = render(tmp, scale)
    gray = arr[..., :3].min(axis=2)
    lines = []
    for name, (x0, y0, x1, y1) in VIEWS.items():
        dark = gray[y0 * scale:y1 * scale, x0 * scale:x1 * scale] < THRESH
        seeds = [(x - x0, y - y0) for x, y in SEE_THROUGH[name]]
        erase = [(a - x0, b - y0, c - x0, d - y0) for a, b, c, d in ERASE[name]]
        mask = silhouette(dark, scale, close, half, min_stroke, seeds, erase)
        out = out_dir / f"cessna_172_{name}_mask.png"
        png_io.write_png(out, (mask * 255).astype(np.uint8))
        rows = np.nonzero(mask.any(axis=1))[0]
        cols = np.nonzero(mask.any(axis=0))[0]
        lines += [f"[views.{name}]", f'file = "cessna_172_{name}_mask.png"',
                  f'sha256 = "{sha(out)}"',
                  f"crop_1x = [{x0}, {y0}, {x1}, {y1}]",
                  f"size_px = [{mask.shape[1]}, {mask.shape[0]}]",
                  f"area_px = {int(mask.sum())}",
                  f"extent_px = {{ left = {cols[0]}, right = {cols[-1]}, "
                  f"top = {rows[0]}, bottom = {rows[-1]} }}", ""]
        if verbose:
            print(f"{name}: {mask.shape[1]}x{mask.shape[0]} px, area {int(mask.sum())}, "
                  f"cols {cols[0]}..{cols[-1]}, rows {rows[0]}..{rows[-1]}")
    head = ["# Provenance of the F1 reference masks. Written by make_reference_masks.py.",
            "[source]", f'svg = "{SVG.name}"', f'svg_sha256 = "{sha(SVG)}"',
            f'tool = "{ver}"', f'command = "{cmd}"',
            f"scale = {scale}", f"close_px = {close}", f"half_stroke_px = {half}",
            f"threshold = {THRESH}", f"min_stroke_px = {min_stroke}", ""]
    for tname, table in (("see_through_seeds_1x", SEE_THROUGH), ("erase_boxes_1x", ERASE)):
        head.append(f"[source.{tname}]")
        head += [f"{k} = {[list(t) for t in v]}" for k, v in table.items()]
        head.append("")
    (out_dir / "cessna_172_reference.toml").write_text("\n".join(head + lines))


def main():
    build(HERE)


if __name__ == "__main__":
    main()
