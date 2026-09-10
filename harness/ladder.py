"""The graded ladder.

A pass rate collapses "did not compile" and "built a beautiful torus with
inverted normals" into the same number. The rung a model reaches says which
mistake it made, which is the whole reason for grading rather than gating.

Rungs are strictly ordered: you cannot reach 5 without 4.
"""

import os

from . import image as img
from . import mesh as msh

RUNGS = [
    (1, "compiles"),
    (2, "runs without crashing"),
    (3, "emits parseable geometry"),
    (4, "geometry is a closed 2-manifold"),
    (5, "topology matches the spec"),
    (6, "renders a non-degenerate image"),
    (7, "image predicates hold"),
]


def _fail(rung, why, detail=None):
    return {"rung": rung - 1, "stopped_at": rung, "why": why,
            "detail": detail or {}, "checks": []}


def evaluate(task, workdir, compile_result, run_result):
    """Return the rung reached and the reason it stopped there."""
    checks = []

    if not compile_result["ok"]:
        return _fail(1, "compile failed", {"stderr": compile_result["stderr"][-4000:]})
    if not run_result["ok"]:
        return _fail(2, run_result.get("why", "process failed"),
                     {"exit": run_result.get("exit"),
                      "stderr": run_result.get("stderr", "")[-4000:]})

    geo_spec = task.get("geometry") or {}
    img_spec = task.get("image") or {}
    obj_name = task.get("obj", "scene.obj")
    img_name = task.get("render", "render.ppm")
    obj_path = os.path.join(workdir, obj_name)
    img_path = os.path.join(workdir, img_name)

    # ---- rung 3
    if not os.path.exists(obj_path):
        return _fail(3, "did not write %s" % obj_name,
                     {"files": sorted(os.listdir(workdir))[:40]})
    try:
        m = msh.Mesh.load(obj_path)
    except msh.MeshError as e:
        return _fail(3, "geometry did not parse: %s" % e)
    desc = msh.describe(obj_path)

    # ---- rung 4
    if geo_spec.get("closed_manifold", True):
        if desc["boundary_edges"]:
            return _fail(4, "surface is open: %d boundary edge(s)" % desc["boundary_edges"], desc)
        if desc["nonmanifold_edges"]:
            return _fail(4, "%d edge(s) shared by more than two faces" % desc["nonmanifold_edges"], desc)
        if not desc["consistently_oriented"]:
            return _fail(4, "face winding is inconsistent across shared edges", desc)
    checks.append("closed 2-manifold")

    # ---- rung 5
    def want(key):
        return geo_spec[key] if key in geo_spec else None

    if want("euler_characteristic") is not None and desc["euler_characteristic"] != geo_spec["euler_characteristic"]:
        return _fail(5, "Euler characteristic is %d, the spec calls for %d"
                     % (desc["euler_characteristic"], geo_spec["euler_characteristic"]), desc)
    if want("shells") is not None and desc["shells"] != geo_spec["shells"]:
        return _fail(5, "mesh has %d shell(s), the spec calls for %d"
                     % (desc["shells"], geo_spec["shells"]), desc)
    if geo_spec.get("normals_outward") and not desc["normals_outward"]:
        return _fail(5, "normals point inward (signed volume %.4f)" % desc["signed_volume"], desc)
    maxdeg = geo_spec.get("max_degenerate", 0)
    if desc["degenerate_faces"] > maxdeg:
        return _fail(5, "%d degenerate triangle(s), at most %d allowed"
                     % (desc["degenerate_faces"], maxdeg), desc)
    vc = geo_spec.get("vertex_count")
    if vc and not (vc["min"] <= desc["vertices"] <= vc["max"]):
        return _fail(5, "%d vertices, the spec calls for %d to %d"
                     % (desc["vertices"], vc["min"], vc["max"]), desc)
    bb = geo_spec.get("bbox")
    if bb:
        tol = bb.get("tol", 0.05)
        for axis, lo_want, hi_want in zip((0, 1, 2), bb["min"], bb["max"]):
            lo_got, hi_got = desc["bbox_min"][axis], desc["bbox_max"][axis]
            if abs(lo_got - lo_want) > tol or abs(hi_got - hi_want) > tol:
                return _fail(5, "bounding box on axis %d is [%.3f, %.3f], the spec calls "
                                "for [%.3f, %.3f] within %.3f"
                             % (axis, lo_got, hi_got, lo_want, hi_want, tol), desc)
    checks.append("topology matches the spec")

    if not img_spec:
        return {"rung": 7, "stopped_at": None, "why": None, "detail": desc, "checks": checks}

    # ---- rung 6
    if not os.path.exists(img_path):
        return _fail(6, "did not write %s" % img_name,
                     {"files": sorted(os.listdir(workdir))[:40], "geometry": desc})
    try:
        idesc = img.describe(img_path)
    except (img.ImageError, Exception) as e:
        return _fail(6, "image did not parse: %s" % e, {"geometry": desc})
    if idesc["coverage"] <= 0.001:
        return _fail(6, "image is empty: nothing but background", {"image": idesc, "geometry": desc})
    if idesc["coverage"] >= 0.999:
        return _fail(6, "image has no background: the frame is entirely covered",
                     {"image": idesc, "geometry": desc})
    checks.append("renders a non-degenerate image")

    # ---- rung 7
    detail = {"geometry": desc, "image": idesc}
    cov = img_spec.get("coverage")
    if cov and not (cov["min"] <= idesc["coverage"] <= cov["max"]):
        return _fail(7, "the object covers %.1f%% of the frame, the spec calls for %.0f to %.0f%%"
                     % (100 * idesc["coverage"], 100 * cov["min"], 100 * cov["max"]), detail)
    if "min_shading_spread" in img_spec and idesc["shading_spread"] < img_spec["min_shading_spread"]:
        return _fail(7, "luminance spread inside the object is %.1f, the spec calls for at least "
                        "%.1f: the silhouette is filled but not lit"
                     % (idesc["shading_spread"], img_spec["min_shading_spread"]), detail)
    if "background_components" in img_spec and idesc["background_components"] != img_spec["background_components"]:
        return _fail(7, "the image has %d background region(s), the spec calls for %d"
                     % (idesc["background_components"], img_spec["background_components"]), detail)
    if "min_silhouette_mirror" in img_spec and idesc["silhouette_mirror"] < img_spec["min_silhouette_mirror"]:
        return _fail(7, "the silhouette overlaps its own mirror image by %.3f, the spec calls "
                        "for at least %.3f: the shape is not left to right symmetric"
                     % (idesc["silhouette_mirror"], img_spec["min_silhouette_mirror"]), detail)
    checks.append("image predicates hold")
    return {"rung": 7, "stopped_at": None, "why": None, "detail": detail, "checks": checks}


def feedback(result, task):
    """The text handed back to the model between iterations.

    States what failed and by how much, and never how to fix it.
    """
    if result["rung"] == 7:
        return "All checks passed."
    rung = result["stopped_at"]
    label = dict(RUNGS)[rung]
    lines = ["Reached rung %d of 7. Failed at rung %d, %s." % (result["rung"], rung, label),
             "", result["why"] or ""]
    d = result.get("detail") or {}
    if "stderr" in d and d["stderr"].strip():
        lines += ["", "Compiler or runtime output:", d["stderr"].strip()]
    geo = d.get("geometry") if isinstance(d.get("geometry"), dict) else (d if "vertices" in d else None)
    if geo:
        lines += ["", "What your geometry actually is:",
                  "  %d vertices, %d faces, %d edges" % (geo["vertices"], geo["faces"], geo["edges"]),
                  "  Euler characteristic %d, genus %.1f, %d shell(s)"
                  % (geo["euler_characteristic"], geo["genus"], geo["shells"]),
                  "  boundary edges %d, non-manifold edges %d, degenerate faces %d"
                  % (geo["boundary_edges"], geo["nonmanifold_edges"], geo["degenerate_faces"]),
                  "  signed volume %.4f, bbox %s to %s"
                  % (geo["signed_volume"],
                     tuple(round(x, 3) for x in geo["bbox_min"]),
                     tuple(round(x, 3) for x in geo["bbox_max"]))]
    im = d.get("image")
    if im:
        lines += ["", "What your image actually is:",
                  "  %dx%d, coverage %.3f, shading spread %.1f, %d luma level(s)"
                  % (im["width"], im["height"], im["coverage"], im["shading_spread"],
                     im["distinct_luma_levels"]),
                  "  background regions %d, silhouette mirror %.3f, luminance mirror %.3f"
                  % (im["background_components"], im["silhouette_mirror"], im["mirror_score"])]
    if "files" in d:
        lines += ["", "Files in the working directory: %s" % ", ".join(d["files"])]
    return "\n".join(lines)
