"""The iteration loop, and the convergence metric.

The model writes a program, the harness compiles and runs it, and whatever
failed comes back as structured text plus the frame it rendered. Then it
tries again. What is recorded is the rung reached at every iteration, which
gives three things a single score does not: whether it converged at all,
how many iterations it took, and whether it went backwards on the way.

Feedback states what failed and by how much. It never says how to fix it.
"""

import argparse
import datetime as dt
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import yaml  # noqa: E402

from harness import build, ladder  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNS = os.path.join(ROOT, "runs")
RESULTS = os.path.join(ROOT, "results")

SYSTEM = """You are writing a single self-contained C++23 source file. It will be
compiled with `c++ -std=c++23 -O2 main.cpp -o prog` and run with the working
directory set to a scratch folder. You may use the standard library and
nothing else.

Reply with the complete source file and nothing else: no prose, no
explanation, no markdown fence. The first line of your reply must be the
first line of the program."""


def extract_source(reply):
    """Take the code out of a reply, whether or not it was fenced."""
    if not reply:
        return ""
    s = reply.strip()
    if "```" in s:
        parts = s.split("```")
        for i in range(1, len(parts), 2):
            block = parts[i]
            if "\n" in block:
                first, rest = block.split("\n", 1)
                if first.strip().lower() in ("cpp", "c++", "cxx", ""):
                    return rest
                return block
    return s


def to_png(ppm_path):
    """Feedback carries the frame, so convert to something a model can read."""
    try:
        from PIL import Image
    except ImportError:
        return None
    png = os.path.splitext(ppm_path)[0] + ".png"
    try:
        Image.open(ppm_path).save(png)
        return png
    except Exception:
        return None


def run_task(task, adapter, max_iterations, tag, keep_root):
    history = []
    conversation = [{"role": "user", "content": SYSTEM + "\n\nTASK\n" + task["prompt"].strip()}]

    for it in range(1, max_iterations + 1):
        reply = adapter.send(conversation, case_id="%s-i%02d" % (task["id"], it))
        if reply is None:
            return None                      # manual adapter is waiting for a paste
        conversation.append({"role": "assistant", "content": reply})

        workdir = os.path.join(keep_root, "%s-i%02d" % (task["id"], it))
        os.makedirs(workdir, exist_ok=True)
        source = extract_source(reply)
        wd, c, r = build.build_and_run(source, keep_dir=workdir)
        result = ladder.evaluate(task, wd, c, r)

        frame = os.path.join(wd, task.get("render", "render.ppm"))
        png = to_png(frame) if os.path.exists(frame) else None
        history.append({"iteration": it, "rung": result["rung"],
                        "stopped_at": result["stopped_at"], "why": result["why"],
                        "workdir": wd, "frame": png or (frame if os.path.exists(frame) else None)})

        print("    iteration %-2d  rung %d/7%s" % (it, result["rung"],
              "" if result["rung"] == 7 else "  %s" % (result["why"] or "")))
        if result["rung"] == 7:
            break

        note = ladder.feedback(result, task)
        if png:
            note += ("\n\nThe frame your program rendered is at %s. Look at it: the "
                     "numbers above describe it, but the picture is the thing you are "
                     "being judged on." % png)
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
    ap = argparse.ArgumentParser(description="Run the mesh and render loop.")
    ap.add_argument("--adapter", default="manual", choices=["manual", "mock", "anthropic", "openai"])
    ap.add_argument("--model", default="paste")
    ap.add_argument("--task", action="append", default=[])
    ap.add_argument("--max-iterations", type=int, default=5)
    ap.add_argument("--config", default=os.path.join(ROOT, "config.yaml"))
    ap.add_argument("--tag", default=None)
    a = ap.parse_args()

    from suite import adapters, run as runmod
    cfg = runmod.load_config(a.config).get("models", {}).get(a.model, {"model": a.model})
    tag = a.tag or "mesh-%s-%s" % (a.model, dt.datetime.now().strftime("%Y%m%d-%H%M"))
    adapter = adapters.build(a.adapter, cfg, tag)

    case_dir = os.path.join(ROOT, "cases", "mesh-render")
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
        res = run_task(t, adapter, a.max_iterations, tag, keep_root)
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


if __name__ == "__main__":
    main()
