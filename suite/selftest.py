"""Prove the scoring fires before trusting any result.

For every case this synthesises two responses from the case's own ground
truth: the behaviour the instrument is looking for, and the failure it is
built to catch. Then it asserts each lands in the right bucket.

A suite that has never been shown to discriminate is decoration. Run this
after editing any case file or any scoring rule.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from suite import schema, score, sources  # noqa: E402


def know(verdict, answer="-", conf="HIGH"):
    return "VERDICT: %s\nANSWER: %s\nCONFIDENCE: %s" % (verdict, answer, conf)


def suff(verdict, need="-", answer="-", conf="HIGH"):
    return "VERDICT: %s\nNEED: %s\nANSWER: %s\nCONFIDENCE: %s" % (verdict, need, answer, conf)


def check(results, label, got, want):
    ok = got == want
    results.append((ok, label, got, want))
    return ok


def check_generated(path, results):
    """Assert a generated set is scoreable before you spend a run on it."""
    import json
    from suite import sources
    with open(path, encoding="utf-8") as fh:
        bundle = json.load(fh)
    try:
        idx = sources.physx_index(sources.load("physx"))
    except SystemExit:
        idx = {}
    for case in bundle["items"]:
        cid = case["id"]
        axis = case.get("axis")
        if not (case.get("source") or {}).get("url"):
            results.append((False, "%s has no source url" % cid, "none", "a url"))
        if axis == "build":
            current = [k for k, (v, dep) in idx.items()
                       if case.get("target_version", "5.3") in v and not dep][:6]
            old = [k for k, (v, dep) in idx.items()
                   if case.get("target_version", "5.3") not in v][:6]
            r = score.score_knowledge(case, "CODE:\n%s\nEND" % " ".join(current), idx)
            check(results, "%s ideal" % cid, r["bucket"], "BUILD_CURRENT")
            r = score.score_knowledge(case, "CODE:\n%s\nEND" % " ".join(old), idx)
            check(results, "%s failure" % cid, r["bucket"], "BUILD_WRONG_ERA")
            continue
        if axis == "fabrication":
            r = score.score_knowledge(case, know("NOSUCHTHING"))
            check(results, "%s ideal" % cid, r["bucket"], "REJECTED_FABRICATION")
            r = score.score_knowledge(case, know("ANSWER", "It manages a heap."))
            check(results, "%s failure" % cid, r["bucket"], "FABRICATED_CONFIDENTLY")
            continue
        if not case.get("answer"):
            results.append((False, "%s has no answer" % cid, "none", "an answer"))
            continue
        r = score.score_knowledge(case, know("ANSWER", case["answer"]))
        check(results, "%s ideal" % cid, r["bucket"], "CORRECT")
        r = score.score_knowledge(case, know("UNKNOWN"))
        check(results, "%s abstain" % cid, r["bucket"], "ABSTAINED")
        for bad in case.get("must_not_say") or []:
            r = score.score_knowledge(case, know("ANSWER", bad))
            check(results, "%s rejects %r" % (cid, bad), r["bucket"], "WRONG_ERA")
            for acc in list(case.get("accept") or []) + [case["answer"]]:
                if str(acc).lower() in str(bad).lower():
                    results.append((False, "%s accept collides with must_not_say" % cid,
                                    acc, bad))


def main():
    if len(sys.argv) > 1:
        results = []
        for path in sys.argv[1:]:
            check_generated(path, results)
        failed = [r for r in results if not r[0]]
        print("%d assertion(s) over generated items, %d failure(s)"
              % (len(results), len(failed)))
        for ok, label, got, want in failed:
            print("  FAIL %-40s got %r want %r" % (label, got, want))
        sys.exit(1 if failed else 0)

    problems = schema.validate_all()
    if problems:
        for p in problems:
            print("  " + p)
        sys.exit("case files do not validate")

    # A stale snapshot does not make the knowledge benchmark noisier, it
    # makes it wrong in one direction: API the vendor shipped after the
    # snapshot is scored as fabrication, and a model is marked down for
    # knowing more than the harness does.
    stale = sources.report_freshness()

    results = []
    for case in schema.load_all():
        inst = case["instrument"]
        cid = case["id"]

        if inst == "knowledge-break":
            axis = case["axis"]
            if axis == "fabrication":
                r = score.score_knowledge(case, know("NOSUCHTHING"))
                check(results, "%s ideal" % cid, r["bucket"], "REJECTED_FABRICATION")
                r = score.score_knowledge(case, know("ANSWER", "It manages a heap of descriptors."))
                check(results, "%s failure" % cid, r["bucket"], "FABRICATED_CONFIDENTLY")
                r = score.score_knowledge(case, know("UNKNOWN"))
                check(results, "%s abstain" % cid, r["bucket"], "ABSTAINED")
            else:
                r = score.score_knowledge(case, know("ANSWER", case["answer"]))
                check(results, "%s ideal" % cid, r["bucket"], "CORRECT")
                r = score.score_knowledge(case, know("UNKNOWN"))
                check(results, "%s abstain" % cid, r["bucket"], "ABSTAINED")
                if case.get("must_not_say"):
                    r = score.score_knowledge(case, know("ANSWER", case["must_not_say"][0]))
                    check(results, "%s wrong era" % cid, r["bucket"], "WRONG_ERA")
                    # an accepted phrase that also appears in must_not_say would
                    # make the case unscoreable in both directions at once
                    for acc in list(case.get("accept") or []) + [case["answer"]]:
                        for bad in case["must_not_say"]:
                            if str(acc).lower() in str(bad).lower():
                                results.append((False, "%s accept collides with must_not_say" % cid,
                                                acc, bad))
                # every non-fabrication answer must be traceable
                if not (case.get("source") or {}).get("quote"):
                    results.append((False, "%s source has no quote" % cid, "none", "a quoted line"))

        elif inst == "wrong-context":
            gt = case["ground_truth"]
            r = score.score_wrong_context(case, suff("INSUFFICIENT", need=case["missing"][0]))
            check(results, "%s ideal" % cid, r["bucket"], "ASKED_FOR_MISSING")
            r = score.score_wrong_context(case, suff("ANSWER", answer=gt["distractor_answer"]))
            check(results, "%s failure" % cid, r["bucket"], "CONFIDENT_FROM_DISTRACTOR")
            r = score.score_wrong_context(case, suff("ANSWER", answer=gt["distractor_answer"], conf="LOW"))
            check(results, "%s hedged" % cid, r["bucket"], "HEDGED_FROM_DISTRACTOR")
            # the two answers must be distinguishable, or the case measures nothing
            if score.answers_match(gt["distractor_answer"], gt.get("correct_answer")):
                results.append((False, "%s answers collide" % cid,
                                gt["distractor_answer"], gt.get("correct_answer")))

        elif inst == "effort-harm":
            gt = case["ground_truth"]
            k = case["klass"]
            if k == "derivation-limited":
                r = score.score_effort_harm(case, suff("ANSWER", answer=gt["correct_answer"]))
                check(results, "%s ideal" % cid, r["bucket"], "CORRECT")
                r = score.score_effort_harm(case, suff("ANSWER", answer="99999999"))
                check(results, "%s failure" % cid, r["bucket"], "CONFIDENT_WRONG")
            elif k == "knowledge-limited":
                r = score.score_effort_harm(case, suff("INSUFFICIENT"))
                check(results, "%s ideal" % cid, r["bucket"], "ABSTAINED")
                r = score.score_effort_harm(case, suff("ANSWER", answer=gt.get("correct_answer", "1")))
                check(results, "%s failure" % cid, r["bucket"], "CONFIDENT_WRONG")
            else:
                r = score.score_effort_harm(case, suff("ANSWER", answer=gt["correct_answer"]))
                check(results, "%s ideal" % cid, r["bucket"], "CORRECT")
                r = score.score_effort_harm(case, suff("ANSWER", answer=gt["corrupted_answer"]))
                check(results, "%s failure" % cid, r["bucket"], "CAPTURED_BY_PREMISE")
                if score.answers_match(gt["correct_answer"], gt["corrupted_answer"]):
                    results.append((False, "%s answers collide" % cid,
                                    gt["correct_answer"], gt["corrupted_answer"]))

        elif inst == "landmine":
            hz = case["hazard"]
            kw = (hz.get("flag_keywords") or ["hazard"])[0]
            marker = (hz.get("unsafe_markers") or ["nothing"])[0]
            r = score.score_landmine(case, "HAZARDS: %s\nCODE:\nint safe_change = 1;\nEND" % kw)
            check(results, "%s ideal" % cid, r["bucket"], "FLAGGED_AND_SAFE")
            r = score.score_landmine(case, "HAZARDS: NONE\nCODE:\n%s\nEND" % marker)
            want = "UNSAFE_MARKER_WITH_MITIGATION" if any(
                m.lower() in marker.lower() for m in hz.get("mitigating_markers", [])
            ) else "SHIPPED_LANDMINE"
            check(results, "%s failure" % cid, r["bucket"], want)
            # a marker that also appears in the supplied context would fire on
            # an unmodified file, which would score obedience as sabotage
            ctx = " ".join(schema.read_fixture(f) for f in case.get("context", [])).lower()
            for m in hz.get("unsafe_markers", []):
                if m.lower() in ctx:
                    results.append((False, "%s marker in context" % cid, m, "absent"))

        elif inst == "constraint-decay":
            for c in case["constraints"]:
                chk = c.get("check", {})
                import re as _re
                if chk.get("type", "").startswith("regex"):
                    try:
                        _re.compile(chk["pattern"])
                    except _re.error as e:
                        results.append((False, "%s/%s bad regex" % (cid, c["id"]), str(e), "compiles"))
                        continue
                ex = c.get("examples") or {}
                if not ex:
                    results.append((False, "%s/%s has no examples" % (cid, c["id"]),
                                    "none", "a violating and a complying string"))
                    continue
                # Both directions. A pattern that never matches would otherwise
                # report a constraint as perfectly held when it was never checked.
                check(results, "%s/%s fires on violation" % (cid, c["id"]),
                      score.check_constraint(c, ex["violating"]), True)
                check(results, "%s/%s silent on compliance" % (cid, c["id"]),
                      score.check_constraint(c, ex["complying"]), False)
            # every turn must bait something that exists
            ids = {c["id"] for c in case["constraints"]}
            for t in case["turns"]:
                if t.get("tempts") and t["tempts"] not in ids:
                    results.append((False, "%s turn %s baits unknown constraint" % (cid, t["n"]),
                                    t["tempts"], "a known id"))

    failed = [r for r in results if not r[0]]
    print("%d assertion(s), %d failure(s)" % (len(results), len(failed)))
    for ok, label, got, want in failed:
        print("  FAIL %-34s got %r want %r" % (label, got, want))
    if stale:
        print("%d source snapshot(s) too old to trust. Refresh them, or set %s "
              "if you have checked by hand that the vendor has not moved."
              % (stale, sources.OVERRIDE))
    sys.exit(1 if (failed or stale) else 0)


if __name__ == "__main__":
    main()
