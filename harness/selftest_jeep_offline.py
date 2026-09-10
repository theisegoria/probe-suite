"""Score the jeep ladder against recorded trajectories, with no PhysX.

The full jeep self test needs a built PhysX 5, which is far too heavy for
continuous integration. So the traces every mutant and reference produced
are recorded once, on a machine that does have it, and this scores them.

That covers rungs 3 to 7, which is where all the ladder's judgement lives.
Rungs 0 to 2 are about compiling and running, so they are exercised with
made up compile and run results instead: a ladder that mishandles a failed
build does not need a physics engine to prove it.

Rebuild the fixtures with:

    python3 harness/selftest_jeep.py --record fixtures/jeep
"""

import gzip
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import yaml  # noqa: E402

from harness import jeep_ladder  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, "fixtures", "jeep")

OK_COMPILE = {"ok": True, "stderr": "", "cmd": "recorded"}
OK_RUN = {"ok": True, "exit": 0, "stdout": "", "stderr": ""}

# (label, fixture file, expected rung)
RECORDED = [
    ("unmodified reference", "unmodified-reference.csv.gz", 7),
    ("second reference, pure pursuit", "second-reference--pure-pursuit.csv.gz", 7),
    ("spawned too high, still falling", "spawned-too-high--still-falling.csv.gz", 3),
    ("never applies throttle", "never-applies-throttle.csv.gz", 4),
    ("steers the wrong way", "steers-the-wrong-way.csv.gz", 5),
    ("gets round, but far too slowly", "gets-round--but-far-too-slowly.csv.gz", 6),
]


def _unpack(path, workdir):
    with gzip.open(path, "rb") as fh, open(os.path.join(workdir, "trace.csv"), "wb") as out:
        shutil.copyfileobj(fh, out)


def main():
    task = yaml.safe_load(open(os.path.join(ROOT, "cases/jeep/jd-01.yaml"), encoding="utf-8"))
    if not os.path.isdir(FIXTURES):
        print("\nno recorded traces at fixtures/jeep. Record them on a machine with "
              "PhysX:\n  python3 harness/selftest_jeep.py --record fixtures/jeep\n")
        return 2

    print("\njeep ladder, offline: recorded trajectories and fabricated build results\n")
    failures = 0
    work = tempfile.mkdtemp(prefix="probe-jeep-offline-")
    try:
        # Rungs 0 to 2 need no trajectory at all.
        cases = [
            ("a build that failed", 0,
             {"ok": False, "stderr": "error: expected ';'", "cmd": "x"}, OK_RUN),
            ("a program that died", 1, OK_COMPILE,
             {"ok": False, "why": "killed by signal 11", "exit": -11, "stderr": ""}),
            ("ran, but wrote no trajectory", 2, OK_COMPILE, OK_RUN),
        ]
        for label, expect, c, r in cases:
            empty = tempfile.mkdtemp(prefix="probe-empty-", dir=work)
            res = jeep_ladder.evaluate(task, empty, c, r)
            ok = res["rung"] == expect
            failures += 0 if ok else 1
            print("  %s  %-34s rung %d (expected %d)%s"
                  % ("ok  " if ok else "FAIL", label, res["rung"], expect,
                     "" if ok else "   <- %s" % (res["why"] or "")))

        for label, name, expect in RECORDED:
            path = os.path.join(FIXTURES, name)
            if not os.path.exists(path):
                print("  FAIL  %-34s fixture missing: %s" % (label, name))
                failures += 1
                continue
            wd = tempfile.mkdtemp(prefix="probe-rec-", dir=work)
            _unpack(path, wd)
            res = jeep_ladder.evaluate(task, wd, OK_COMPILE, OK_RUN)
            ok = res["rung"] == expect
            failures += 0 if ok else 1
            print("  %s  %-34s rung %d (expected %d)%s"
                  % ("ok  " if ok else "FAIL", label, res["rung"], expect,
                     "" if ok else "   <- %s" % (res["why"] or "")))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    total = len(RECORDED) + 3
    print("\n%d case(s), %d failure(s)" % (total, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
