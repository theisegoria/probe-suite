"""The jeep iteration loop.

Same shape as the mesh loop: the model writes a program, the harness builds
it against PhysX, runs it, and hands back what failed and by how much. What
is recorded is the rung at every iteration, so a run says whether the model
converged, how long it took, and whether it went backwards on the way.

Feedback here is the trajectory, not a picture. It says what the vehicle
actually did: how far it got, how fast, how close to rolling. It never says
how to fix it.
"""

import argparse
import datetime as dt
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import yaml  # noqa: E402

from harness import build_jeep, jeep_ladder, physx_env  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNS = os.path.join(ROOT, "runs")
RESULTS = os.path.join(ROOT, "results")

SYSTEM = """You are writing a single self-contained C++ source file. It will be
compiled against a built PhysX 5 SDK, with the include paths and libraries
supplied for you, and run with the working directory set to a scratch folder
that contains the assets named in the task.

Reply with the complete source file and nothing else: no prose, no
explanation, no markdown fence. The first line of your reply must be the
first line of the program."""


def run_task(task, adapter, max_iterations, keep_root):
    from suite.mesh_loop import extract_source

    history = []
    conversation = [{"role": "user", "content": SYSTEM + "\n\nTASK\n" + task["prompt"].strip()}]

    for it in range(1, max_iterations + 1):
        reply = adapter.send(conversation, case_id="%s-i%02d" % (task["id"], it))
        if reply is None:
            return None                      # manual adapter is waiting for a paste
        conversation.append({"role": "assistant", "content": reply})

        workdir = os.path.join(keep_root, "%s-i%02d" % (task["id"], it))
        os.makedirs(workdir, exist_ok=True)
        wd, c, r = build_jeep.build_and_run(extract_source(reply), keep_dir=workdir)
        result = jeep_ladder.evaluate(task, wd, c, r)

        trace = os.path.join(wd, task.get("trace", "trace.csv"))
        history.append({"iteration": it, "rung": result["rung"],
                        "stopped_at": result["stopped_at"], "why": result["why"],
                        "workdir": wd, "detail": result.get("detail", {}),
                        "trace": trace if os.path.exists(trace) else None})

        print("    iteration %-2d  rung %d/7%s" % (it, result["rung"],
              "" if result["rung"] == 7 else "  %s" % (result["why"] or "")))
        if result["rung"] == 7:
            break

        note = jeep_ladder.feedback(result, task)
        note += "\n\nReply with the complete corrected source file and nothing else."
        conversation.append({"role": "user", "content": note})

    rungs = [h["rung"] for h in history]
    converged = next((h["iteration"] for h in history if h["rung"] == 7), None)
    return {"task": task["id"], "title": task.get("title", ""),
            "iterations": len(history), "converged_at": converged,
            "best_rung": max(rungs) if rungs else 0,
            "final_rung": rungs[-1] if rungs else 0,
            "regressed": any(b < a for a, b in zip(rungs, rungs[1:])),
            "history": history}


def main():
    ap = argparse.ArgumentParser(description="Run the jeep test drive loop.")
    ap.add_argument("--adapter", default="manual", choices=["manual", "mock", "anthropic", "openai"])
    ap.add_argument("--model", default="paste")
    ap.add_argument("--task", action="append", default=[])
    ap.add_argument("--max-iterations", type=int, default=5)
    ap.add_argument("--config", default=os.path.join(ROOT, "config.yaml"))
    ap.add_argument("--tag", default=None)
    a = ap.parse_args()

    try:
        env = physx_env.describe()
    except physx_env.PhysXMissing as e:
        print("\n%s\n" % e)
        return 2
    print("  PhysX libraries: %s" % env["libdir"])

    from suite import adapters, run as runmod
    cfg = runmod.load_config(a.config).get("models", {}).get(a.model, {"model": a.model})
    tag = a.tag or "jeep-%s-%s" % (a.model, dt.datetime.now().strftime("%Y%m%d-%H%M"))
    adapter = adapters.build(a.adapter, cfg, tag)

    case_dir = os.path.join(ROOT, "cases", "jeep")
    tasks = []
    for f in sorted(os.listdir(case_dir)):
        if f.endswith((".yaml", ".yml")):
            t = yaml.safe_load(open(os.path.join(case_dir, f), encoding="utf-8"))
            if not a.task or t["id"] in a.task:
                tasks.append(t)
    if not tasks:
        sys.exit("no tasks matched")

    keep_root = os.path.join(RUNS, tag, "work")
    os.makedirs(keep_root, exist_ok=True)
    out = []
    for t in tasks:
        print("  %s  %s" % (t["id"], t.get("title", "")))
        res = run_task(t, adapter, a.max_iterations, keep_root)
        if res:
            out.append(res)

    if out:
        os.makedirs(RESULTS, exist_ok=True)
        path = os.path.join(RESULTS, tag + ".json")
        json.dump({"tag": tag, "model": a.model, "adapter": a.adapter,
                   "when": dt.datetime.now().isoformat(timespec="seconds"),
                   "tasks": out}, open(path, "w", encoding="utf-8"), indent=1)
        print("\n  %-8s %-22s %-9s %-11s %s" % ("task", "title", "best rung", "converged", "regressed"))
        for r in out:
            print("  %-8s %-22s %-9s %-11s %s"
                  % (r["task"], r["title"][:22], "%d/7" % r["best_rung"],
                     ("iteration %d" % r["converged_at"]) if r["converged_at"] else "no",
                     "yes" if r["regressed"] else "no"))
        print("\n  results/%s.json" % tag)
    if getattr(adapter, "pending", None):
        print("\n  %d prompt(s) waiting for a reply in runs/%s/" % (len(adapter.pending), tag))
    return 0


if __name__ == "__main__":
    sys.exit(main())
