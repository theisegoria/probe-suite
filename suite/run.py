"""Administer cases and record raw replies.

Nothing is scored here. A run produces a results file of raw responses so
that scoring can be re-run, and the scoring rules revised, without paying
for the model calls again.
"""

import argparse
import datetime as dt
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from suite import adapters, prompts, schema  # noqa: E402

try:
    import yaml
except ImportError:
    sys.exit("PyYAML is required: pip install pyyaml")

ROOT = schema.ROOT
RESULTS = os.path.join(ROOT, "results")


def load_config(path):
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as fh:
        return yaml.safe_load(fh) or {}


def run_single(ad, case, effort, tag):
    prompt = prompts.compose(case)
    reply = ad.send([{"role": "user", "content": prompt}],
                    effort=effort, case_id=case["id"])
    rec = {"case": case["id"], "instrument": case["instrument"],
           "klass": case.get("klass"), "tier": case.get("tier"),
           "effort": effort, "reply": reply}
    if case.get("domain"):
        rec["case_data"] = case
    return rec


def run_session(ad, case, effort, tag):
    """Constraint decay: one conversation, many turns, history carried."""
    history = [{"role": "user", "content": prompts.compose_constraints(case)}]
    ack = ad.send(history, effort=effort, case_id=case["id"], turn=0)
    if ack is None:
        return None
    history.append({"role": "assistant", "content": ack})
    turns = []
    for turn in case["turns"]:
        history.append({"role": "user", "content": prompts.compose_turn(case, turn)})
        reply = ad.send(history, effort=effort, case_id=case["id"], turn=turn["n"])
        if reply is None:
            return None
        history.append({"role": "assistant", "content": reply})
        turns.append({"n": turn["n"], "tempts": turn.get("tempts"), "reply": reply})
    return {"case": case["id"], "instrument": case["instrument"],
            "effort": effort, "turns": turns}


def main():
    ap = argparse.ArgumentParser(description="Administer suite cases.")
    ap.add_argument("--adapter", default="manual",
                    choices=["manual", "mock", "anthropic", "openai"])
    ap.add_argument("--model", default="unnamed",
                    help="Key into config.yaml models, and the label in reports")
    ap.add_argument("--effort", action="append", default=[],
                    help="Repeatable. Sweeps every level given.")
    ap.add_argument("--instrument", action="append", default=[],
                    choices=list(schema.INSTRUMENTS))
    ap.add_argument("--case", action="append", default=[])
    ap.add_argument("--items", default=None,
                    help="A generated item set from suite/generate.py. "
                         "Replaces the on-disk cases for this run.")
    ap.add_argument("--config", default=os.path.join(ROOT, "config.yaml"))
    ap.add_argument("--tag", default=None, help="Run tag, defaults to model+timestamp")
    a = ap.parse_args()

    problems = [] if a.items else schema.validate_all()
    if problems:
        for p in problems:
            print("  " + p)
        sys.exit("fix the case files first")

    cfg = load_config(a.config).get("models", {}).get(a.model, {"model": a.model})
    efforts = a.effort or [None]
    tag = a.tag or "%s-%s" % (a.model, dt.datetime.now().strftime("%Y%m%d-%H%M"))
    ad = adapters.build(a.adapter, cfg, tag)

    if a.items:
        with open(a.items, encoding="utf-8") as fh:
            bundle = json.load(fh)
        cases = bundle["items"]
        print("administering %d generated item(s), seed %s" % (len(cases), bundle.get("seed")))
    else:
        cases = schema.load_all(a.instrument or None, a.case or None)
    if not cases:
        sys.exit("no cases matched")

    records, incomplete = [], 0
    for effort in efforts:
        for case in cases:
            if case["instrument"] == "constraint-decay":
                rec = run_session(ad, case, effort, tag)
            else:
                rec = run_single(ad, case, effort, tag)
            if rec is None or rec.get("reply") is None:
                incomplete += 1
                continue
            records.append(rec)

    os.makedirs(RESULTS, exist_ok=True)
    out = os.path.join(RESULTS, "%s.json" % tag)
    with open(out, "w", encoding="utf-8") as fh:
        json.dump({"tag": tag, "model": a.model, "adapter": a.adapter,
                   "efforts": efforts, "when": dt.datetime.now().isoformat(timespec="seconds"),
                   "records": records}, fh, indent=1)

    print("%d response(s) recorded to results/%s.json" % (len(records), tag))
    if getattr(ad, "pending", None):
        print("\n%d prompt(s) are waiting for a reply in runs/%s/" % (len(ad.pending), tag))
        print("Paste each .prompt.txt into the model, save the reply beside it")
        print("as the matching .reply.txt, then run this command again.")
    elif incomplete:
        print("%d case(s) produced no response" % incomplete)


if __name__ == "__main__":
    main()
