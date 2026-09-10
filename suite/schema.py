"""Case loading and validation.

Every case is a YAML file under cases/<instrument>/. A case says what
context the model is given, what it is asked, and what the ground truth
is. The harness never decides correctness by judgement where a rule will
do, so every case carries the machinery for its own scoring.
"""

import os
import sys

try:
    import yaml
except ImportError:
    sys.exit("PyYAML is required: pip install pyyaml")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASE_DIR = os.path.join(ROOT, "cases")
FIXTURE_DIR = os.path.join(ROOT, "fixtures")

INSTRUMENTS = ("knowledge-break", "wrong-context", "effort-harm",
               "constraint-decay", "landmine")

REQUIRED = {
    "knowledge-break": ["id", "axis", "question", "source"],
    "wrong-context": ["id", "tier", "context", "missing", "question", "ground_truth"],
    "effort-harm": ["id", "klass", "context", "question", "ground_truth"],
    "constraint-decay": ["id", "constraints", "turns"],
    "landmine": ["id", "task", "context", "hazard"],
}

KLASSES = ("derivation-limited", "knowledge-limited", "premise-corrupted")

AXES = ("recency", "coverage", "version-delta", "neighbour", "depth", "fabrication")


class CaseError(Exception):
    pass


def read_fixture(rel):
    path = os.path.join(FIXTURE_DIR, rel)
    if not os.path.exists(path):
        raise CaseError("fixture missing: %s" % rel)
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def load_case(path):
    with open(path, encoding="utf-8") as fh:
        case = yaml.safe_load(fh)
    if not isinstance(case, dict):
        raise CaseError("%s is not a mapping" % path)
    instrument = os.path.basename(os.path.dirname(path))
    case["instrument"] = instrument
    case["_path"] = path
    for field in REQUIRED[instrument]:
        if field not in case:
            raise CaseError("%s: missing field %r" % (path, field))
    if instrument == "knowledge-break":
        if case["axis"] not in AXES:
            raise CaseError("%s: axis must be one of %s" % (path, ", ".join(AXES)))
        src = case.get("source") or {}
        for f in ("url", "checked"):
            if not src.get(f):
                raise CaseError("%s: source.%s is required, every answer must be "
                                "traceable to a primary source" % (path, f))
        if case["axis"] == "fabrication":
            if case.get("exists") is not False:
                raise CaseError("%s: a fabrication case must set exists: false" % path)
        elif not case.get("answer"):
            raise CaseError("%s: missing answer" % path)
    if instrument == "effort-harm" and case["klass"] not in KLASSES:
        raise CaseError("%s: klass must be one of %s" % (path, ", ".join(KLASSES)))
    if instrument in ("wrong-context", "effort-harm", "landmine"):
        for rel in case.get("context", []):
            read_fixture(rel)
        for rel in case.get("missing", []):
            read_fixture(rel)
    return case


def load_all(instruments=None, ids=None):
    cases = []
    for inst in (instruments or INSTRUMENTS):
        d = os.path.join(CASE_DIR, inst)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.endswith((".yaml", ".yml")):
                continue
            case = load_case(os.path.join(d, f))
            if ids and case["id"] not in ids:
                continue
            cases.append(case)
    return cases


def validate_all():
    """Load every case and report problems. Exit code is the problem count."""
    problems = []
    seen = {}
    for inst in INSTRUMENTS:
        d = os.path.join(CASE_DIR, inst)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.endswith((".yaml", ".yml")):
                continue
            p = os.path.join(d, f)
            try:
                case = load_case(p)
            except CaseError as e:
                problems.append(str(e))
                continue
            if case["id"] in seen:
                problems.append("duplicate id %s in %s and %s"
                                % (case["id"], seen[case["id"]], p))
            seen[case["id"]] = p
    return problems


if __name__ == "__main__":
    probs = validate_all()
    if probs:
        for p in probs:
            print("  " + p)
        print("%d problem(s)" % len(probs))
        sys.exit(1)
    print("all cases valid")
