"""The jeep ladder.

Same idea as the mesh ladder: strictly ordered rungs, so the number says
which mistake was made. The early rungs are deliberately fine grained,
because a hard task where everything dies at rung 2 tells you nothing.

  1 compiles
  2 runs without crashing
  3 writes a parseable trajectory
  4 the vehicle is stable at rest
  5 the vehicle responds to its controls
  6 it completes the course
  7 it completes upright, in bounds, and inside the time budget
"""

import os

from . import trace as tr

RUNGS = [
    (1, "compiles"),
    (2, "runs without crashing"),
    (3, "writes a parseable trajectory"),
    (4, "the vehicle is stable at rest"),
    (5, "the vehicle responds to its controls"),
    (6, "completes the course"),
    (7, "completes upright, in bounds, inside the budget"),
]


def _fail(rung, why, detail=None):
    return {"rung": rung - 1, "stopped_at": rung, "why": why, "detail": detail or {}, "checks": []}


def evaluate(task, workdir, compile_result, run_result):
    checks = []
    if not compile_result["ok"]:
        return _fail(1, "compile failed", {"stderr": compile_result["stderr"][-4000:]})
    if not run_result["ok"]:
        return _fail(2, run_result.get("why", "process failed"),
                     {"exit": run_result.get("exit"), "stderr": run_result.get("stderr", "")[-4000:]})

    name = task.get("trace", "trace.csv")
    path = os.path.join(workdir, name)
    if not os.path.exists(path):
        return _fail(3, "did not write %s" % name, {"files": sorted(os.listdir(workdir))[:40]})
    try:
        t = tr.Trace.load(path)
    except tr.TraceError as e:
        return _fail(3, "trajectory did not parse: %s" % e)
    d = tr.describe(path)

    spec = task.get("checks", {})
    rest = spec.get("rest", {})
    rest_until = rest.get("until_t", 2.0)

    # ---- rung 4: does it sit still on its wheels before being asked to do anything
    drift = t.rest_drift(rest_until)
    if drift > rest.get("max_drift", 0.5):
        return _fail(4, "the vehicle moved %.2f m in the first %.1f s without being driven, "
                        "at most %.2f m allowed: it is sinking, sliding or being launched"
                     % (drift, rest_until, rest.get("max_drift", 0.5)), d)
    rest_tilt = t.max_tilt(None, rest_until)
    if rest_tilt > rest.get("max_tilt_deg", 12.0):
        return _fail(4, "the vehicle tilted %.1f degrees while at rest, at most %.1f allowed: "
                        "it is not sitting on its wheels"
                     % (rest_tilt, rest.get("max_tilt_deg", 12.0)), d)
    y0 = t.rows[0]["py"]
    band = rest.get("spawn_y_band")
    if band and not (band[0] <= y0 <= band[1]):
        return _fail(4, "the vehicle starts at y = %.2f, expected between %.2f and %.2f: it has "
                        "fallen through the terrain or is floating above it"
                     % (y0, band[0], band[1]), d)
    checks.append("stable at rest")

    # ---- rung 5: does it move, and does steering do anything
    move = spec.get("responds", {})
    if d["max_speed"] < move.get("min_speed", 1.0):
        return _fail(5, "top speed was %.2f m/s, at least %.2f expected: the vehicle never "
                        "meaningfully moved under throttle"
                     % (d["max_speed"], move.get("min_speed", 1.0)), d)
    if d["heading_change_deg"] < move.get("min_heading_change_deg", 20.0):
        return _fail(5, "total heading change was %.1f degrees, at least %.1f expected: the "
                        "vehicle drives but does not steer"
                     % (d["heading_change_deg"], move.get("min_heading_change_deg", 20.0)), d)
    checks.append("responds to controls")

    # ---- rung 6: the course
    wps = task.get("waypoints", [])
    hits = t.waypoint_progress(wps)
    reached = sum(1 for h in hits if h is not None)
    d["waypoints_reached"] = reached
    d["waypoints_total"] = len(wps)
    d["waypoint_times"] = hits
    if reached < len(wps):
        nxt = wps[reached]
        last = t.rows[-1]
        return _fail(6, "reached %d of %d waypoints. Never got within %.1f m of waypoint %d at "
                        "x=%.1f z=%.1f; the run ended at x=%.1f z=%.1f"
                     % (reached, len(wps), nxt.get("radius", 3.0), reached + 1,
                        nxt["x"], nxt["z"], last["px"], last["pz"]), d)
    checks.append("completes the course")

    # ---- rung 7: and does it in one piece
    fin = spec.get("finish", {})
    max_tilt = t.max_tilt()
    if max_tilt > fin.get("max_tilt_deg", 55.0):
        return _fail(7, "tilted %.1f degrees during the run, at most %.1f allowed: it rolled or "
                        "very nearly did" % (max_tilt, fin.get("max_tilt_deg", 55.0)), d)
    bounds = task.get("bounds")
    if bounds:
        bad = t.out_of_bounds(bounds)
        if bad:
            return _fail(7, "left the course at t=%.2f, x=%.1f y=%.1f z=%.1f"
                         % (bad["t"], bad["px"], bad["py"], bad["pz"]), d)
    budget = fin.get("time_budget_s")
    finish_t = hits[-1] if hits else None
    d["finish_t"] = finish_t
    if budget and finish_t is not None and finish_t > budget:
        return _fail(7, "finished the course at t=%.1f s, the budget is %.1f s"
                     % (finish_t, budget), d)
    checks.append("finished upright, in bounds, inside the budget")
    return {"rung": 7, "stopped_at": None, "why": None, "detail": d, "checks": checks}


def feedback(result, task):
    if result["rung"] == 7:
        return "All checks passed."
    rung = result["stopped_at"]
    lines = ["Reached rung %d of 7. Failed at rung %d, %s."
             % (result["rung"], rung, dict(RUNGS)[rung]), "", result["why"] or ""]
    d = result.get("detail") or {}
    if d.get("stderr", "").strip():
        lines += ["", "Compiler or runtime output:", d["stderr"].strip()]
    if "rows" in d:
        lines += ["", "What your run actually did:",
                  "  %d ticks over %.2f s, travelled %.1f m, top speed %.2f m/s"
                  % (d["rows"], d["duration"], d["distance"], d["max_speed"]),
                  "  start %s, end %s"
                  % (tuple(round(x, 2) for x in d["start"]), tuple(round(x, 2) for x in d["end"])),
                  "  maximum tilt from upright %.1f degrees, total heading change %.1f degrees"
                  % (d["max_tilt_deg"], d["heading_change_deg"])]
        if "waypoints_total" in d:
            lines.append("  waypoints reached %d of %d"
                         % (d["waypoints_reached"], d["waypoints_total"]))
    if "files" in d:
        lines += ["", "Files in the working directory: %s" % ", ".join(d["files"])]
    return "\n".join(lines)
