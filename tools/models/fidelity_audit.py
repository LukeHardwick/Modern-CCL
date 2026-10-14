# Deterministic fidelity checker for the high-fidelity Cessna (task T22e): does the model look
# like the real aircraft? Gates F1-F5 (docs/research/05-cessna-fidelity.md):
#   F1 outline against the reference three-view   F2 feature checklist
#   F3 cross-section shape                        F4 UVs, textures, file budget
#   F5 edge crispness (bevel radii, creases)
#
#   python3 tools/models/fidelity_audit.py --model M.glb --config tools/models/audit/cessna_fidelity.toml \
#       --out DIR [--name NAME] [--family textured|vertex_colour] [--images]
#   (or: blender --background --factory-startup --python tools/models/fidelity_audit.py -- ...)
#
# Writes DIR/<name>_fidelity.json (byte-identical for the same inputs) and DIR/<name>_fidelity.md;
# with --images also DIR/<name>_f1_<view>.png overlays (grey: both, red: reference only, blue:
# model only). Exit status 1 when a hard gate fails or cannot be measured, 2 on bad input or an
# internal error (traceback printed, so a crash never looks like a pass). Pure Python + numpy
# (+ Blender's bpy only to decode JPEG textures). No randomness, no timestamps, no absolute
# paths in the reports. Frame: metres, x = vehicle left, y = aft (nose at -y), z = up.

import argparse
import hashlib
import json
import sys
import tomllib
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True

import fidelity_features  # noqa: E402
import fidelity_model as fm  # noqa: E402
import fidelity_outline  # noqa: E402
import fidelity_section  # noqa: E402
import fidelity_tex  # noqa: E402
import glb_io  # noqa: E402

TOOL_VERSION = 1
REPO = HERE.parent.parent

TOP_KEYS = {"meta", "model", "gates", "F1_outline", "F1_stations", "F2_features", "F2_feature",
            "F3_sections", "F3_section", "F4_textures", "F5_edges", "F5_edge", "visual_review"}
REQUIRED = ("model", "gates", "F1_outline", "F2_features", "F3_sections", "F4_textures", "F5_edges")


def load_config(path):
    raw = Path(path).read_bytes()
    cfg = tomllib.loads(raw.decode("utf-8"))
    unknown = sorted(set(cfg) - TOP_KEYS)
    if unknown:
        raise fm.ConfigError(f"unknown config section(s): {', '.join(unknown)}")
    for k in REQUIRED:
        if k not in cfg:
            raise fm.ConfigError(f"missing config section [{k}]")
    names = [f["name"] for f in cfg.get("F2_feature", [])]
    if len(set(names)) != len(names):
        raise fm.ConfigError("two [[F2_feature]] entries share a name")
    for st in cfg.get("F1_stations", []):
        if st.get("view") not in ("side", "top", "front"):
            raise fm.ConfigError(f"station {st.get('name')!r}: view must be side, top or front")
    cfg["_sha256"] = hashlib.sha256(raw).hexdigest()
    return cfg


def summarise(gates):
    passed = sum(1 for g in gates if g["status"] == "pass")
    failed = [g for g in gates if g["status"] == "fail"]
    na = [g for g in gates if g["status"] == "na"]
    hard_failed = [g for g in gates if g["hard"] and g["status"] in ("fail", "na")]
    return {"gates": len(gates), "passed": passed, "failed": len(failed), "not_applicable": len(na),
            "hard_failed": len(hard_failed),
            "by_group": {grp: {"pass": sum(1 for g in gates if g["id"].startswith(grp + ".")
                                           and g["status"] == "pass"),
                               "total": sum(1 for g in gates if g["id"].startswith(grp + "."))}
                         for grp in ("F1", "F2", "F3", "F4", "F5")}}


def audit(model_path, cfg, family=None, root=REPO, keep_masks=False):
    data = fm.ModelData(model_path)
    gates, detail = [], {}
    g, d, masks = fidelity_outline.run(data, cfg, root, keep_masks)
    gates += g
    detail["F1"] = d
    g, d = fidelity_features.run(data, cfg)
    gates += g
    detail["F2"] = d
    g, d = fidelity_section.run_sections(data, cfg)
    gates += g
    detail["F3"] = d
    g, d = fidelity_tex.run(data, cfg, family)
    gates += g
    detail["F4"] = d
    g, d = fidelity_section.run_edges(data, cfg)
    gates += g
    detail["F5"] = d
    return data, gates, detail, masks


def result(data, gates, detail, cfg, model_path, config_path, family):
    return {
        "tool": {"name": "fidelity_audit", "version": TOOL_VERSION},
        "frame": "metres, x = vehicle left, y = aft (nose at -y), z = up",
        "inputs": {"model": {"file": Path(model_path).name, "sha256": data.sha256,
                             "bytes": data.size},
                   "config": {"file": Path(config_path).name, "sha256": cfg["_sha256"],
                              "status": cfg.get("meta", {}).get("status")},
                   "family": family or cfg["F4_textures"].get("family", "textured")},
        "model": {"triangles": data.triangles, "primitives": len(data.prims),
                  "nodes": len(data.glb.nodes), "materials": len(data.glb.materials),
                  "images": data.image_count(),
                  "has_uvs": any(p.uvs is not None for p in data.prims)},
        "summary": summarise(gates), "gates": gates, "detail": detail,
        "warnings": list(data.glb.warnings)}


def dump_json(res):
    return json.dumps(res, sort_keys=True, indent=1, ensure_ascii=True, allow_nan=False) + "\n"


def fmt(v, unit):
    if v is None:
        return "-"
    if isinstance(v, list):
        return "[" + ", ".join(fmt(x, unit) for x in v) + "]"
    if isinstance(v, dict):
        return ", ".join(f"{k} {x}" for k, x in v.items())
    if unit == "bytes":
        return f"{v / 1e6:.2f} MB"
    if unit == "frac":
        return f"{v * 100:.2f} %"
    if unit == "ratio":
        return f"{v:.3f}"
    if unit in ("mm", "px", "px/m", "count", "uv"):
        return f"{v:,}".replace(",", " ") if isinstance(v, int) else f"{v:g}"
    return f"{v:g}" if isinstance(v, float) else str(v)


def write_md(res, name):
    s, inp = res["summary"], res["inputs"]
    L = [f"# Fidelity audit: {name}", ""]
    L.append(f"Model `{inp['model']['file']}` ({inp['model']['bytes'] / 1e6:.2f} MB, sha256 "
             f"`{inp['model']['sha256'][:12]}`), config `{inp['config']['file']}` "
             f"(sha256 `{inp['config']['sha256'][:12]}`, status: {inp['config']['status']}), "
             f"family **{inp['family']}**. Coordinates: {res['frame']}.")
    L.append("")
    verdict = "PASS" if s["hard_failed"] == 0 and s["failed"] == 0 else (
        "FAIL (hard)" if s["hard_failed"] else "FAIL")
    L.append(f"**{s['passed']} of {s['gates']} gates pass; {s['failed']} fail, "
             f"{s['not_applicable']} not applicable or not measurable; {s['hard_failed']} hard "
             f"failures.** Overall: **{verdict}**.")
    L.append("")
    L.append("| Group | Pass | Total |")
    L.append("|---|---|---|")
    for grp, v in s["by_group"].items():
        L.append(f"| {grp} | {v['pass']} | {v['total']} |")
    L.append("")
    L.append("| Gate | Value | Limit | Result |")
    L.append("|---|---|---|---|")
    for g in res["gates"]:
        hard = " (hard)" if g["hard"] else ""
        r = {"pass": "PASS", "fail": f"**FAIL{hard}**",
             "na": f"**N.A.{hard}**"}[g["status"]]
        if g.get("reason"):
            r += f": {g['reason']}"
        limit = "-" if g["limit"] is None else f"{g['op']} {fmt(g['limit'], g['unit'])}"
        val = g["value"]
        if isinstance(val, dict):
            val = ", ".join(f"{k} {v}" for k, v in val.items())
        else:
            val = fmt(val, g["unit"])
        L.append(f"| {g['id']}: {g['label']} | {val} | {limit} | {r} |")
    L.append("")
    f1 = res["detail"]["F1"]
    L.append("## F1 detail")
    L.append("")
    L.append("| View | IoU | model->ref p95 / max (mm) | ref->model p95 / max (mm) | ref scale (mm/px h, v) |")
    L.append("|---|---|---|---|---|")
    for view, v in f1["views"].items():
        a, b = v["model_to_reference_mm"], v["reference_to_model_mm"]
        L.append(f"| {view} | {v['iou']:.4f} | {a['p95']:.1f} / {a['max']:.1f} | "
                 f"{b['p95']:.1f} / {b['max']:.1f} | {v['reference_m_per_px']['h'] * 1000:.2f}, "
                 f"{v['reference_m_per_px']['v'] * 1000:.2f} |")
    L.append("")
    L.append("| Station | View | Model (m) | Reference (m) | Delta (mm) |")
    L.append("|---|---|---|---|---|")
    for st in f1["stations"]:
        L.append(f"| {st['name']} | {st['view']} | {st['model_m']} | {st['reference_m']} | {st['delta_mm']} |")
    L.append("")
    L.append("## F3 sections")
    L.append("")
    L.append("| Section | Width (m) | Height (m) | Ratio | Fill | Flat (mm) | Min radius (mm) |")
    L.append("|---|---|---|---|---|---|---|")
    for sec in res["detail"]["F3"]:
        if not sec.get("found"):
            L.append(f"| {sec['name']} | not found | | | | | |")
            continue
        flat = "-" if "flat_m" not in sec else f"{sec['flat_m'] * 1000:.1f}"
        rad = "-" if "radius_m" not in sec else f"{sec['radius_m'] * 1000:.1f}"
        L.append(f"| {sec['name']} | {sec['width_m']} | {sec['height_m']} | {sec['ratio']} | "
                 f"{sec['fill']} | {flat} | {rad} |")
    L.append("")
    L.append("## F4 sheets")
    L.append("")
    sheets = res["detail"]["F4"].get("sheets", [])
    if not sheets:
        L.append("No UV sheets (the model has no UVs, or the vertex-colour family was audited).")
    else:
        L.append("| Sheet | Size | Triangles | Density p5 / p95 (px/m) | Overlap px | Padding ok (px) |")
        L.append("|---|---|---|---|---|---|")
        for sh in sheets:
            L.append(f"| {sh['name']} | {sh['size_px'][0]}x{sh['size_px'][1]}"
                     f"{'' if sh['has_image'] else ' (assumed)'} | {sh['triangles']} | "
                     f"{sh.get('density_p5')} / {sh.get('density_p95')} | {sh['overlap_px']} | "
                     f"{sh['padding_ok_px']} |")
    L.append("")
    if res["warnings"]:
        L.append("## Warnings")
        L.append("")
        L += [f"- {w}" for w in res["warnings"]]
        L.append("")
    return "\n".join(L)


def main(argv=None):
    if argv is None:
        argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else (
            [] if "bpy" in sys.modules else sys.argv[1:])
    try:
        return run_cli(argv)
    except (glb_io.GlbError, fm.ConfigError, tomllib.TOMLDecodeError, OSError) as exc:
        print(f"[fidelity] error: {exc}", file=sys.stderr)
        return 2
    except Exception:  # noqa: BLE001
        traceback.print_exc()
        print("[fidelity] internal error (traceback above)", file=sys.stderr)
        return 2


def run_cli(argv):
    ap = argparse.ArgumentParser(prog="fidelity_audit.py", description=(
        "Deterministic fidelity checker for the high-fidelity Cessna: gates F1 outline, F2 features, "
        "F3 sections, F4 UVs/textures/budget, F5 edges. See the header of this file."))
    ap.add_argument("--model", required=True, type=Path)
    ap.add_argument("--config", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--name", help="report name (default: the model's file name stem)")
    ap.add_argument("--family", choices=("textured", "vertex_colour"),
                    help="file family (default: config [F4_textures].family)")
    ap.add_argument("--images", action="store_true", help="also write F1 overlay PNGs")
    ap.add_argument("--root", type=Path, default=REPO, help="repository root (reference files)")
    args = ap.parse_args(argv)
    cfg = load_config(args.config)
    name = args.name or args.model.stem
    data, gates, detail, masks = audit(args.model, cfg, args.family, args.root, args.images)
    res = result(data, gates, detail, cfg, args.model, args.config, args.family)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / f"{name}_fidelity.json").write_text(dump_json(res))
    (args.out / f"{name}_fidelity.md").write_text(write_md(res, name))
    if args.images:
        for view, (mm, rm_, _grid) in masks.items():
            fidelity_outline.overlay_png(args.out / f"{name}_f1_{view}.png", mm, rm_)
    s = res["summary"]
    print(f"[fidelity] {name}: {s['passed']}/{s['gates']} gates pass, {s['failed']} fail, "
          f"{s['not_applicable']} n.a., {s['hard_failed']} hard; report "
          f"{args.out / (name + '_fidelity.md')}")
    return 1 if s["hard_failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
