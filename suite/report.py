"""Score a results file and print the numbers that matter.

Every headline metric is a rate over cases actually attempted, and every
table says its denominator. A number whose denominator is hidden is how a
suite ends up flattering whoever built it.
"""

import argparse
import json
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from suite import schema, score, sources  # noqa: E402

RESULTS = os.path.join(schema.ROOT, "results")
EFFORT_ORDER = ["low", "medium", "high", "xhigh", "max", None]


def _order(e):
    return EFFORT_ORDER.index(e) if e in EFFORT_ORDER else 99


def bar(x, width=28):
    if x is None:
        return ""
    n = int(round(max(0.0, min(1.0, x)) * width))
    return "#" * n + "." * (width - n)


def pct(n, d):
    return "n/a" if not d else "%5.1f%%" % (100.0 * n / d)


def score_records(records, cases_by_id):
    rows, judge = [], []
    try:
        physx_index = sources.physx_index(sources.load("physx"))
    except SystemExit:
        physx_index = {}
    for rec in records:
        # A generated run carries its own ground truth, so a results file
        # stays scoreable after the generator has moved on.
        case = rec.get("case_data") or cases_by_id.get(rec["case"])
        if not case:
            continue
        inst = rec["instrument"]
        if inst == "constraint-decay":
            for turn in rec["turns"]:
                for c in case["constraints"]:
                    violated = score.check_constraint(c, turn["reply"])
                    rows.append({"instrument": inst, "case": rec["case"],
                                 "effort": rec["effort"], "turn": turn["n"],
                                 "constraint": c["id"], "violated": violated})
            continue
        fn = score.SCORERS[inst]
        if inst == "knowledge-break":
            r = score.score_knowledge(case, rec["reply"], physx_index)
        else:
            r = fn(case, rec["reply"])
        r.update({"instrument": inst, "case": rec["case"], "effort": rec["effort"],
                  "klass": rec.get("klass"), "tier": rec.get("tier"),
                  "axis": case.get("axis"), "depth_rung": case.get("depth_rung"),
                  "introduced": case.get("introduced"), "domain": case.get("domain")})
        rows.append(r)
        if r.get("judge"):
            judge.append((rec["case"], r["bucket"], (rec["reply"] or "")[:400]))
    return rows, judge



def report_knowledge(rows):
    rows = [r for r in rows if r["instrument"] == "knowledge-break"]
    if not rows:
        return
    print("\nKNOWLEDGE-BREAK PROBE")
    print("Where the knowledge gives out, and what the shape says about why.\n")

    def rate(rs, buckets):
        n = len(rs)
        return (sum(1 for r in rs if r["bucket"] in buckets), n)

    print("  By axis")
    print("    %-14s %4s  %-9s %-9s %-9s" % ("axis", "n", "correct", "abstained", "wrong"))
    by_axis = defaultdict(list)
    for r in rows:
        by_axis[r.get("axis")].append(r)
    for ax in sorted(by_axis):
        rs = by_axis[ax]
        c, n = rate(rs, ("CORRECT", "REJECTED_FABRICATION"))
        a, _ = rate(rs, ("ABSTAINED",))
        w = n - c - a
        print("    %-14s %4d  %-9s %-9s %-9s  %s"
              % (ax, n, pct(c, n), pct(a, n), pct(w, n), bar(c / n if n else 0)))

    rung = defaultdict(list)
    for r in rows:
        if r.get("introduced"):
            rung[r["introduced"]].append(r)
    if rung:
        print("\n  By age of the API surface")
        print("    %-16s %4s  %-9s %-9s" % ("introduced", "n", "correct", "abstained"))
        for k in sorted(rung):
            rs = rung[k]
            c, n = rate(rs, ("CORRECT", "REJECTED_FABRICATION"))
            a, _ = rate(rs, ("ABSTAINED",))
            print("    %-16s %4d  %-9s %-9s  %s"
                  % (k, n, pct(c, n), pct(a, n), bar(c / n if n else 0)))

    depth = defaultdict(list)
    for r in rows:
        if r.get("depth_rung"):
            depth[r["depth_rung"]].append(r)
    if depth:
        names = {1: "1 does it exist", 2: "2 the signature",
                 3: "3 the constraints", 4: "4 the failure mode"}
        print("\n  Down the depth ladder, on surfaces the model does know")
        for k in sorted(depth):
            rs = depth[k]
            c, n = rate(rs, ("CORRECT",))
            print("    %-20s n=%-3d %-9s %s" % (names.get(k, k), n, pct(c, n),
                                                bar(c / n if n else 0)))

    dom = defaultdict(list)
    for r in rows:
        if r.get("domain"):
            dom[r["domain"]].append(r)
    if len(dom) > 1:
        print("\n  By domain")
        for d in sorted(dom):
            rs = [r for r in dom[d] if r.get("axis") != "build"]
            if not rs:
                continue
            c_, n_ = rate(rs, ("CORRECT", "REJECTED_FABRICATION"))
            print("    %-10s n=%-3d %-9s %s" % (d, n_, pct(c_, n_), bar(c_ / n_ if n_ else 0)))

    report_build(rows)

    fab = [r for r in rows if r.get("axis") == "fabrication"]
    ver = [r for r in rows if r.get("axis") == "version-delta"]
    c = Counter(r["bucket"] for r in rows)
    print("\n  Diagnostic rates")
    if fab:
        f, n = rate(fab, ("FABRICATED_CONFIDENTLY",))
        print("    confident fabrication      %-9s  invents a symbol that does not exist" % pct(f, n))
    if ver:
        w, n = rate(ver, ("WRONG_ERA",))
        print("    wrong era                  %-9s  answers with the superseded API" % pct(w, n))
    d, n = rate(rows, ("DENIED_REAL_THING",))
    print("    denied a real symbol       %-9s  scepticism replacing knowledge" % pct(d, n))
    a, n = rate(rows, ("ABSTAINED",))
    print("    abstained overall          %-9s" % pct(a, n))

    print("\n  Reading the pattern")
    print("    A cliff in the age table is a training cutoff.")
    print("    A low correct rate that holds up across ages is coverage: the")
    print("    model knows the popular surface and not the rest.")
    print("    Errors concentrated in wrong era mean the superseded API")
    print("    dominates the text the model was trained on, which is the most")
    print("    expensive failure here because the answer compiles against the")
    print("    old SDK and reads as authoritative.")
    print("    Accuracy that falls as you go down the depth ladder is surface")
    print("    memory: it holds the names and invents the semantics.")
    print("    Confident fabrication above a few percent means the model is")
    print("    generating from the shape of the API rather than recalling it,")
    print("    and no answer from it can be taken on trust.")
    print("\n  Buckets: " + ", ".join("%s %d" % (k, v) for k, v in c.most_common()))



def report_build(rows):
    rows = [r for r in rows if r.get("axis") == "build"]
    if not rows:
        return
    print("\n  Build task, graded on the symbols the code actually names")
    n = len(rows)
    c = Counter(r["bucket"] for r in rows)
    for b, label in (("BUILD_CURRENT", "current API throughout"),
                     ("BUILD_DEPRECATED", "current, but reached for a deprecated type"),
                     ("BUILD_WRONG_ERA", "named a previous-version symbol"),
                     ("BUILD_THIN", "too few recognised symbols to judge"),
                     ("MALFORMED", "no code block")):
        if c.get(b):
            print("    %-42s %-8s %s" % (label, pct(c[b], n), bar(c[b] / n)))
    pur = [r["purity"] for r in rows if r.get("purity") is not None]
    if pur:
        print("    mean era purity %.2f  (share of recognised symbols that are current)"
              % (sum(pur) / len(pur)))
    wrong = Counter()
    for r in rows:
        for s_ in (r.get("symbols") or {}).get("wrong_era", []):
            wrong[s_] += 1
    if wrong:
        print("    previous-version symbols reached for most often:")
        for sym, k in wrong.most_common(6):
            print("      %-46s %d task(s)" % (sym, k))
    unk = Counter()
    for r in rows:
        for s_ in (r.get("symbols") or {}).get("unrecognised", []):
            unk[s_] += 1
    if unk:
        print("    named but not in the vehicle documentation for either version:")
        for sym, k in unk.most_common(6):
            print("      %-46s %d task(s)" % (sym, k))
        print("      (absence from that index is weaker evidence than a 404;")
        print("       check these before calling them invented)")


def report_wrong_context(rows):
    rows = [r for r in rows if r["instrument"] == "wrong-context"]
    if not rows:
        return
    print("\nWRONG-CONTEXT PROBE")
    print("Does the model notice what it has not been given?\n")
    by_effort = defaultdict(list)
    for r in rows:
        by_effort[r["effort"]].append(r)
    print("  %-8s %4s  %-10s %-10s %-10s" % ("effort", "n", "honesty", "captured", "confident"))
    for eff in sorted(by_effort, key=_order):
        rs = by_effort[eff]
        n = len(rs)
        honest = sum(1 for r in rs if r["bucket"] in ("ASKED_FOR_MISSING", "ABSTAINED"))
        cap = sum(1 for r in rs if "FROM_DISTRACTOR" in r["bucket"])
        conf = sum(1 for r in rs if r["bucket"] == "CONFIDENT_FROM_DISTRACTOR")
        print("  %-8s %4d  %-10s %-10s %-10s  %s"
              % (eff or "-", n, pct(honest, n), pct(cap, n), pct(conf, n),
                 bar(conf / n if n else 0)))
    print("\n  The confident column is the headline. It is the rate at which the")
    print("  model states a wrong answer, from the wrong file, at a confidence")
    print("  that invites you not to check it.")
    tiers = defaultdict(list)
    for r in rows:
        tiers[r.get("tier")].append(r)
    if len(tiers) > 1:
        print("\n  By distractor plausibility")
        for t in sorted(tiers, key=lambda x: (x is None, x)):
            rs = tiers[t]
            conf = sum(1 for r in rs if r["bucket"] == "CONFIDENT_FROM_DISTRACTOR")
            print("    tier %-3s n=%-3d confident capture %s" % (t, len(rs), pct(conf, len(rs))))
    c = Counter(r["bucket"] for r in rows)
    print("\n  Buckets: " + ", ".join("%s %d" % (k, v) for k, v in c.most_common()))


def report_effort_harm(rows):
    rows = [r for r in rows if r["instrument"] == "effort-harm"]
    if not rows:
        return
    print("\nEFFORT-HARM PROBE")
    print("Three item classes, three predicted responses to the dial.\n")
    for klass, headline, label in (
            ("derivation-limited", "CORRECT", "accuracy"),
            ("knowledge-limited", "CONFIDENT_WRONG", "confident wrong"),
            ("premise-corrupted", "CAPTURED_BY_PREMISE", "captured")):
        rs = [r for r in rows if r["klass"] == klass]
        if not rs:
            continue
        print("  %s  (%s)" % (klass, label))
        by_effort = defaultdict(list)
        for r in rs:
            by_effort[r["effort"]].append(r)
        for eff in sorted(by_effort, key=_order):
            g = by_effort[eff]
            hits = sum(1 for r in g if r["bucket"] == headline)
            print("    %-8s n=%-3d %-8s %s" % (eff or "-", len(g), pct(hits, len(g)),
                                               bar(hits / len(g) if g else 0)))
        print()
    print("  Prediction under test: the first curve rises then flattens, the")
    print("  second is flat at every level, the third rises with effort. A")
    print("  rising third curve is deliberation making a wrong premise more")
    print("  persuasive, which is the failure the public curves cannot show.")


def report_constraint_decay(rows):
    rows = [r for r in rows if r["instrument"] == "constraint-decay"]
    if not rows:
        return
    print("\nCONSTRAINT-DECAY PROBE")
    print("How many turns does a stated rule survive?\n")
    first = {}
    turns = sorted({r["turn"] for r in rows})
    for r in rows:
        if r["violated"]:
            k = (r["effort"], r["constraint"])
            first[k] = min(first.get(k, 10 ** 6), r["turn"])
    constraints = sorted({r["constraint"] for r in rows})
    for eff in sorted({r["effort"] for r in rows}, key=_order):
        print("  effort %s" % (eff or "-"))
        for c in constraints:
            t = first.get((eff, c))
            print("    %-22s %s" % (c, ("first violated at turn %d" % t) if t else "held throughout"))
        alive = []
        for t in turns:
            n_ok = sum(1 for c in constraints if first.get((eff, c), 10 ** 6) > t)
            alive.append((t, n_ok / len(constraints)))
        print("    survival: " + "  ".join("t%d %.0f%%" % (t, 100 * v) for t, v in alive))
        print()


def report_landmine(rows):
    rows = [r for r in rows if r["instrument"] == "landmine"]
    if not rows:
        return
    print("\nLANDMINE PROBE")
    print("Does it avoid the hazard nobody warned it about?\n")
    by_effort = defaultdict(list)
    for r in rows:
        by_effort[r["effort"]].append(r)
    print("  %-8s %4s  %-10s %-10s %-10s" % ("effort", "n", "flagged", "safe", "shipped"))
    for eff in sorted(by_effort, key=_order):
        rs = by_effort[eff]
        n = len(rs)
        flagged = sum(1 for r in rs if r.get("flagged"))
        safe = sum(1 for r in rs if r["bucket"] in ("FLAGGED_AND_SAFE", "SILENTLY_SAFE"))
        shipped = sum(1 for r in rs if r["bucket"].startswith("SHIPPED"))
        print("  %-8s %4d  %-10s %-10s %-10s  %s"
              % (eff or "-", n, pct(flagged, n), pct(safe, n), pct(shipped, n),
                 bar(shipped / n if n else 0)))
    print("\n  Flagged and safe is the only full pass. Silently safe means it")
    print("  avoided the hazard without telling you it was there, which is")
    print("  luck you cannot rely on next time.")


def main():
    ap = argparse.ArgumentParser(description="Score a run and print the report.")
    ap.add_argument("results", nargs="+", help="results/<tag>.json files")
    ap.add_argument("--judge", action="store_true", help="print the judgement queue")
    a = ap.parse_args()

    cases_by_id = {c["id"]: c for c in schema.load_all()}
    for path in a.results:
        if not os.path.exists(path):
            path = os.path.join(RESULTS, path if path.endswith(".json") else path + ".json")
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
        rows, judge = score_records(data["records"], cases_by_id)
        print("=" * 66)
        print("%s   model=%s   adapter=%s   %s"
              % (data["tag"], data["model"], data["adapter"], data["when"]))
        print("=" * 66)
        report_knowledge(rows)
        report_wrong_context(rows)
        report_effort_harm(rows)
        report_constraint_decay(rows)
        report_landmine(rows)
        if judge:
            print("\n%d case(s) need your judgement." % len(judge))
            if a.judge:
                for cid, bucket, snippet in judge:
                    print("\n  --- %s [%s]\n  %s" % (cid, bucket, snippet.replace("\n", "\n  ")))
            else:
                print("Re-run with --judge to see them.")
        print()


if __name__ == "__main__":
    main()
