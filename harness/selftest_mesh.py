"""Prove the ladder discriminates before trusting a rung it reports.

Two halves, because a ladder can fail in two opposite directions.

Rejection: take the reference solution, break it in one specific way per
rung, and assert each mutant stops exactly where it should. A ladder that
reported rung 7 for everything, or rung 0 for everything, would look
identical to a working one in a summary table.

Acceptance: run a second reference written to share as little as possible
with the first, and assert it also reaches the top. Mutants alone cannot
catch a ladder tuned so tightly around one solution that it fails every
model for reasons that are the benchmark's fault.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import yaml  # noqa: E402

from harness import build, ladder  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (label, expected rung reached, substitution)
MUTANTS = [
    ("unmodified reference", 7, None),
    ("does not compile", 0,
     ("int main() {", "int main() { this is not c++;")),
    ("compiles but exits non zero", 1,
     ("int main() {", "int main() { if (true) return 3;")),
    ("writes no geometry", 2,
     ('std::fopen("scene.obj", "w")', 'std::fopen("/dev/null", "w")')),
    ("open surface, not closed", 3,
     ("for (int i = 0; i < NU; ++i)\n        for (int j = 0; j < NV; ++j) {\n            const int a",
      "for (int i = 0; i < NU - 1; ++i)\n        for (int j = 0; j < NV - 1; ++j) {\n            const int a")),
    ("closed but normals inward", 4,
     ("if (vol < 0)", "if (vol > 0)")),
    ("geometry right, renders nothing", 5,
     ('std::fopen("render.ppm", "wb")', 'std::fopen("/dev/null", "wb")')),
    ("renders a flat unlit silhouette", 6,
     ("const double shade = 0.18 + 0.82 * lam;", "const double shade = 1.0; (void)lam;")),
]


# Independent solutions that must also reach the top. Not variations of the
# reference: different construction, different topology order, different
# renderer.
ALTERNATES = [
    ("second reference, swept and painted", "harness/reference/mr-01b.cpp"),
]


def main():
    task = yaml.safe_load(open(os.path.join(ROOT, "cases/mesh-render/mr-01.yaml"), encoding="utf-8"))
    base = open(os.path.join(ROOT, "harness/reference/mr-01.cpp"), encoding="utf-8").read()

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
        wd, c, r = build.build_and_run(src)
        try:
            res = ladder.evaluate(task, wd, c, r)
            got = res["rung"]
            ok = got == expect
            failures += 0 if ok else 1
            print("  %s  %-34s rung %d (expected %d)%s"
                  % ("ok  " if ok else "FAIL", label, got, expect,
                     "" if ok else "   <- %s" % (res["why"] or "")))
        finally:
            build.cleanup(wd)

    for label, path in ALTERNATES:
        src = open(os.path.join(ROOT, path), encoding="utf-8").read()
        wd, c, r = build.build_and_run(src)
        try:
            res = ladder.evaluate(task, wd, c, r)
            ok = res["rung"] == 7
            failures += 0 if ok else 1
            print("  %s  %-34s rung %d (expected 7)%s"
                  % ("ok  " if ok else "FAIL", label, res["rung"],
                     "" if ok else "   <- %s" % (res["why"] or "")))
        finally:
            build.cleanup(wd)

    print("\n%d mutant(s), %d alternate(s), %d failure(s)"
          % (len(MUTANTS), len(ALTERNATES), failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
