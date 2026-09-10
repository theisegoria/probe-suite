"""Prove the jeep ladder discriminates before trusting a rung it reports.

One mutant per rung, each one a plausible way a real submission goes wrong:
a program that will not build, one that dies, one that writes nothing, a
vehicle that will not sit still, one that will not move, one that moves but
cannot follow the course, and one that gets round but takes all day. Each
mutant has to stop exactly where it should. A ladder that answered 7 to
everything, or 0 to everything, would look the same in a results table.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import yaml  # noqa: E402

from harness import build_jeep, jeep_ladder, physx_env  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (label, expected rung reached, substitution)
MUTANTS = [
    ("unmodified reference", 7, None),
    ("does not compile", 0,
     ("int main() {", "int main() { this is not c++;")),
    ("compiles but exits non zero", 1,
     ("int main() {", "int main() { if (true) return 3;")),
    ("writes no trajectory", 2,
     ('std::fopen("trace.csv", "w")', 'std::fopen("/dev/null", "w")')),
    ("spawned too high, still falling", 3,
     ("PxVec3(0.0f, 0.95f, 0.0f)", "PxVec3(0.0f, 9.0f, 0.0f)")),
    ("never applies throttle", 4,
     ("gVehicle.mCommandState.throttle = throttle;",
      "gVehicle.mCommandState.throttle = 0.0f;")),
    ("steers the wrong way", 5,
     ("steer = std::clamp(err / (35.0f * PxPi / 180.0f), -1.0f, 1.0f);",
      "steer = std::clamp(-err / (35.0f * PxPi / 180.0f), -1.0f, 1.0f);")),
    ("gets round, but far too slowly", 6,
     ("const float settleUntil = 2.0f;", "const float settleUntil = 55.0f;")),
]


def main():
    try:
        physx_env.describe()
    except physx_env.PhysXMissing as e:
        print("\ncannot run the jeep self test:\n  %s\n" % e)
        return 2

    task = yaml.safe_load(open(os.path.join(ROOT, "cases/jeep/jd-01.yaml"), encoding="utf-8"))
    base = open(os.path.join(ROOT, "harness/reference/jd-01.cpp"), encoding="utf-8").read()

    print("\njeep ladder self test: %d mutants of the reference\n" % len(MUTANTS))
    failures = 0
    for label, expect, sub in MUTANTS:
        src = base
        if sub:
            old, new = sub
            if old not in src:
                print("  SETUP FAIL  %-34s pattern not found in the reference" % label)
                failures += 1
                continue
            src = src.replace(old, new, 1)
        wd, c, r = build_jeep.build_and_run(src)
        try:
            res = jeep_ladder.evaluate(task, wd, c, r)
            got = res["rung"]
            ok = got == expect
            failures += 0 if ok else 1
            print("  %s  %-34s rung %d (expected %d)%s"
                  % ("ok  " if ok else "FAIL", label, got, expect,
                     "" if ok else "   <- %s" % (res["why"] or "")))
        finally:
            build_jeep.cleanup(wd)

    print("\n%d mutant(s), %d failure(s)" % (len(MUTANTS), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
