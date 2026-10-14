# Noise floor of gate F1 (task T22e): how much do the F1 numbers move for reasons that are not
# the model's shape? Run once per model change of the checker or the reference masks; the
# result is committed with the baseline.
#
#   python3 tools/models/fidelity_noise.py --model databases/sample/models/cessna.glb \
#       --config tools/models/audit/cessna_fidelity.toml --out DIR
#
# Three studies, all deterministic:
#  1. raster pitch: the same model and reference on a 2.5 / 5 / 10 mm grid;
#  2. reference extraction: the drawing re-rasterised and re-filled with other render scales,
#     bridge radii and stroke half-widths (tools/models/reference/make_reference_masks.py);
#  3. offset curve: the reference shifted outwards / inwards by d mm used as a "model": the IoU
#     and outline distance a uniform error of d mm produces (this relates the IoU gates to the
#     outline-distance gates);
# plus the drawing's own consistency (the scale of one axis measured in two views).

import argparse
import json
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "reference"))
sys.dont_write_bytecode = True

import fidelity_audit as fa  # noqa: E402
import fidelity_model as fm  # noqa: E402
import fidelity_outline as fo  # noqa: E402
import make_reference_masks as mrm  # noqa: E402
import raster_util as ru  # noqa: E402

VIEWS = ("side", "top", "front")


def f1_numbers(data, cfg, root, ref_rel, pitch_cm):
    cfg = json.loads(json.dumps({k: v for k, v in cfg.items() if not k.startswith("_")}))
    cfg["F1_outline"]["reference"] = ref_rel
    cfg["F1_outline"]["raster_px_per_cm"] = pitch_cm
    gates, det, _ = fo.run(data, cfg, root)
    out = {}
    for v in VIEWS:
        d = det["views"][v]
        out[v] = {"iou": d["iou"], "p95_mm": d["model_to_reference_mm"]["p95"],
                  "max_mm": d["model_to_reference_mm"]["max"]}
    out["stations"] = {s["name"]: s["delta_mm"] for s in det["stations"]}
    return out


def offset_curve(data, cfg, root, offsets_mm):
    cfgc = {k: v for k, v in cfg.items() if not k.startswith("_")}
    f1 = cfgc["F1_outline"]
    ref, ref_masks = fo.load_reference(Path(root) / f1["reference"])
    pitch = 0.01 / f1["raster_px_per_cm"]
    rows = {}
    for v in VIEWS:
        _p, _t, anchors, transform, _s, grid, _e = fo.view_setup(data, cfgc, ref, v, pitch)
        rmask = fo.sample_reference(ref_masks[v], transform, grid)
        rows[v] = []
        for d in offsets_mm:
            px = int(round(abs(d) / 1000.0 / pitch))
            m = ru.dilate(rmask, px) if d > 0 else ru.erode(rmask, px) if d < 0 else rmask
            iou, m2r, _r2m = fo.compare_masks(m, rmask, grid)
            rows[v].append({"offset_mm": d, "iou": round(iou, 4), "p95_mm": round(m2r["p95"] * 1000, 1),
                            "max_mm": round(m2r["max"] * 1000, 1)})
    return rows


def run(model, config, out):
    cfg = fa.load_config(config)
    root = fa.REPO
    data = fm.ModelData(model)
    res = {"model": Path(model).name, "pitch": {}, "extraction": {}, "offset_curve": {}, "reference": {}}
    ref_rel = cfg["F1_outline"]["reference"]
    for px_cm in (4, 2, 1):
        res["pitch"][f"{10 / px_cm:g} mm"] = f1_numbers(data, cfg, root, ref_rel, px_cm)
    variants = [("scale 2, close 2.5, half 1.5 (committed)", 2, 2.5, 1.5),
                ("scale 3", 3, 2.5, 1.5), ("scale 4", 4, 2.5, 1.5),
                ("close 2.0", 2, 2.0, 1.5), ("close 3.0", 2, 3.0, 1.5),
                ("half 1.0", 2, 2.5, 1.0), ("half 2.0", 2, 2.5, 2.0)]
    for name, scale, close, half in variants:
        with tempfile.TemporaryDirectory() as tmp:
            mrm.build(tmp, scale, close, half, verbose=False)
            res["extraction"][name] = f1_numbers(data, cfg, tmp, "cessna_172_reference.toml", 2)
    res["offset_curve"] = offset_curve(data, cfg, root, [-40, -30, -20, -10, -5, 0, 5, 10, 20, 30, 40, 60, 80])
    ref, _m = fo.load_reference(root / ref_rel)
    pub = cfg["model"]["published_size"]
    ext = {v: ref["views"][v]["extent_px"] for v in VIEWS}
    wpx = {v: ext[v]["right"] - ext[v]["left"] + 1 for v in VIEWS}
    hpx = {v: ext[v]["bottom"] - ext[v]["top"] + 1 for v in VIEWS}
    # drawing consistency: metres per drawing pixel of one axis from two views (published dims)
    res["reference"] = {
        "length_mm_per_px": {"side": round(pub["length"] / wpx["side"] * 1000, 3),
                             "top": round(pub["length"] / hpx["top"] * 1000, 3)},
        "span_mm_per_px": {"top": round(pub["span"] / wpx["top"] * 1000, 3),
                           "front": round(pub["span"] / wpx["front"] * 1000, 3)},
        "height_mm_per_px": {"side": round(pub["height"] / hpx["side"] * 1000, 3),
                             "front": round(pub["height"] / hpx["front"] * 1000, 3)}}
    r = res["reference"]
    r["length_disagreement"] = round(r["length_mm_per_px"]["top"] / r["length_mm_per_px"]["side"] - 1, 4)
    r["span_disagreement"] = round(r["span_mm_per_px"]["front"] / r["span_mm_per_px"]["top"] - 1, 4)
    r["height_disagreement"] = round(r["height_mm_per_px"]["front"] / r["height_mm_per_px"]["side"] - 1, 4)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "fidelity_noise.json").write_text(json.dumps(res, sort_keys=True, indent=1) + "\n")
    (out / "fidelity_noise.md").write_text(write_md(res))
    return res


def write_md(res):
    L = ["# F1 noise floor", "",
         f"Model `{res['model']}`; reference masks from drawing A. All numbers are deterministic.", "",
         "## 1. Raster pitch (same model, same reference)", "",
         "| Pitch | side IoU / p95 / max | top IoU / p95 / max | front IoU / p95 / max |", "|---|---|---|---|"]
    for k, v in res["pitch"].items():
        L.append(f"| {k} | " + " | ".join(
            f"{v[w]['iou']:.4f} / {v[w]['p95_mm']:.0f} / {v[w]['max_mm']:.0f}" for w in VIEWS) + " |")
    L += ["", "## 2. Reference extraction (the drawing re-processed; IoU / p95 mm / max mm, model T22d)", "",
          "| Variant | side | top | front |", "|---|---|---|---|"]
    for k, v in res["extraction"].items():
        L.append(f"| {k} | " + " | ".join(
            f"{v[w]['iou']:.4f} / {v[w]['p95_mm']:.0f} / {v[w]['max_mm']:.0f}" for w in VIEWS) + " |")
    spread = {}
    for w in VIEWS:
        for key in ("iou", "p95_mm", "max_mm"):
            vals = [v[w][key] for v in res["extraction"].values()]
            spread[(w, key)] = (min(vals), max(vals))
    L += ["", "Spread over the variants (min .. max): " + "; ".join(
        f"{w}: IoU {spread[(w, 'iou')][0]:.4f}..{spread[(w, 'iou')][1]:.4f}, p95 "
        f"{spread[(w, 'p95_mm')][0]:.0f}..{spread[(w, 'p95_mm')][1]:.0f} mm" for w in VIEWS) + ".", ""]
    st_names = list(next(iter(res["extraction"].values()))["stations"])
    L += ["Station delta (mm) spread over the variants:", "", "| Station | min | max |", "|---|---|---|"]
    for n in st_names:
        vals = [v["stations"][n] for v in res["extraction"].values() if v["stations"][n] is not None]
        L.append(f"| {n} | {min(vals):.1f} | {max(vals):.1f} |")
    L += ["", "## 3. Offset curve: the reference grown (+) or shrunk (-) by d mm, used as the model", "",
          "| d (mm) | side IoU / p95 | top IoU / p95 | front IoU / p95 |", "|---|---|---|---|"]
    n = len(res["offset_curve"]["side"])
    for i in range(n):
        d = res["offset_curve"]["side"][i]["offset_mm"]
        L.append(f"| {d} | " + " | ".join(
            f"{res['offset_curve'][w][i]['iou']:.4f} / {res['offset_curve'][w][i]['p95_mm']:.0f}" for w in VIEWS) + " |")
    r = res["reference"]
    L += ["", "## 4. The drawing's own consistency (published size on each axis, mm per drawing pixel)", "",
          f"- length: side {r['length_mm_per_px']['side']}, top {r['length_mm_per_px']['top']} "
          f"({r['length_disagreement'] * 100:+.1f} %)",
          f"- span: top {r['span_mm_per_px']['top']}, front {r['span_mm_per_px']['front']} "
          f"({r['span_disagreement'] * 100:+.1f} %)",
          f"- height: side {r['height_mm_per_px']['side']}, front {r['height_mm_per_px']['front']} "
          f"({r['height_disagreement'] * 100:+.1f} %)", ""]
    return "\n".join(L)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True, type=Path)
    ap.add_argument("--config", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args(argv)
    run(args.model, args.config, args.out)
    print(f"[noise] wrote {args.out / 'fidelity_noise.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
