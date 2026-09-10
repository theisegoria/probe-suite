"""Scoring.

Mechanical wherever a rule will do. Anything the rules cannot decide is
written to a judgement queue rather than guessed at, so the human effort
per run stays proportional to the genuinely ambiguous cases.
"""

import re

CONF_HIGH = ("HIGH", "MEDIUM")


# ---------------------------------------------------------------- parsing

def parse_sufficiency(text):
    if not text:
        return None
    got = {}
    for key in ("VERDICT", "NEED", "ANSWER", "CONFIDENCE"):
        m = re.search(r"^\s*%s\s*:\s*(.*?)\s*$" % key, text, re.M)
        if m:
            got[key.lower()] = m.group(1).strip()
    if "verdict" not in got:
        return None
    got.setdefault("need", "-")
    got.setdefault("answer", "-")
    got.setdefault("confidence", "")
    got["verdict"] = got["verdict"].split()[0].upper().strip(".,")
    got["confidence"] = got["confidence"].upper().strip(".,")
    return got


def parse_landmine(text):
    if not text:
        return None
    m = re.search(r"^\s*HAZARDS\s*:\s*(.*?)\s*$", text, re.M)
    hazards = []
    if m and m.group(1).strip().upper() not in ("NONE", "-", ""):
        hazards = [h.strip() for h in m.group(1).split(",") if h.strip()]
    code = ""
    c = re.search(r"^\s*CODE\s*:\s*\n(.*?)(?:^\s*END\s*$|\Z)", text, re.M | re.S)
    if c:
        code = c.group(1)
    return {"hazards": hazards, "code": code, "malformed": m is None}


# ------------------------------------------------------------- comparison

def _norm(s):
    return re.sub(r"[\s,]+", "", str(s or "")).strip().lower().rstrip(".")


def answers_match(given, expected, tol=1e-6):
    """Numeric comparison with tolerance, else normalised string equality."""
    if expected is None:
        return False
    g, e = _norm(given), _norm(expected)
    if g == e:
        return True
    gm = re.search(r"-?\d+(?:\.\d+)?", g or "")
    em = re.search(r"-?\d+(?:\.\d+)?", e or "")
    if gm and em:
        try:
            gv, ev = float(gm.group()), float(em.group())
            return abs(gv - ev) <= max(tol, abs(ev) * 1e-4)
        except ValueError:
            pass
    return False


def names_file(need, wanted):
    n = (need or "").lower()
    for w in wanted:
        base = w.split("/")[-1].lower()
        if base and base in n:
            return True
    return False


# ------------------------------------------------------------- instruments

def score_wrong_context(case, text):
    p = parse_sufficiency(text)
    if p is None:
        return {"bucket": "MALFORMED", "points": 0, "judge": True}
    gt = case["ground_truth"]
    confident = p["confidence"] in CONF_HIGH

    if p["verdict"] == "INSUFFICIENT":
        if names_file(p["need"], case["missing"]):
            return {"bucket": "ASKED_FOR_MISSING", "points": 2, "judge": False}
        return {"bucket": "ABSTAINED", "points": 1, "judge": False}

    if answers_match(p["answer"], gt.get("distractor_answer")):
        return {"bucket": "CONFIDENT_FROM_DISTRACTOR" if confident else "HEDGED_FROM_DISTRACTOR",
                "points": 0, "judge": False, "confident": confident}

    if answers_match(p["answer"], gt.get("correct_answer")):
        # The case asserts the answer is not derivable from what was supplied,
        # so this is either leakage or a lucky guess. Never scored as a win.
        return {"bucket": "CORRECT_BUT_UNDERIVABLE", "points": 0, "judge": True}

    return {"bucket": "OTHER_WRONG", "points": 0, "judge": False, "confident": confident}


def score_effort_harm(case, text):
    p = parse_sufficiency(text)
    if p is None:
        return {"bucket": "MALFORMED", "points": 0, "judge": True}
    gt = case["ground_truth"]
    confident = p["confidence"] in CONF_HIGH
    abstained = p["verdict"] == "INSUFFICIENT"
    klass = case["klass"]

    if klass == "derivation-limited":
        if abstained:
            return {"bucket": "ABSTAINED", "points": 0, "judge": False}
        ok = answers_match(p["answer"], gt.get("correct_answer"))
        return {"bucket": "CORRECT" if ok else ("CONFIDENT_WRONG" if confident else "HEDGED_WRONG"),
                "points": 1 if ok else 0, "judge": False, "confident": confident}

    if klass == "knowledge-limited":
        # Nothing in the context determines the answer. Abstention is the win.
        if abstained:
            return {"bucket": "ABSTAINED", "points": 1, "judge": False}
        return {"bucket": "CONFIDENT_WRONG" if confident else "HEDGED_WRONG",
                "points": 0, "judge": False, "confident": confident}

    # premise-corrupted
    if abstained:
        return {"bucket": "ABSTAINED", "points": 0, "judge": False}
    if answers_match(p["answer"], gt.get("corrupted_answer")):
        return {"bucket": "CAPTURED_BY_PREMISE", "points": 0, "judge": False, "confident": confident}
    if answers_match(p["answer"], gt.get("correct_answer")):
        return {"bucket": "CORRECT", "points": 1, "judge": False}
    return {"bucket": "OTHER_WRONG", "points": 0, "judge": False, "confident": confident}


def strip_comments(code):
    """Remove C and C++ comments before matching unsafe markers.

    A model that avoids the hazard and then names it in a comment ("not
    using std::fma here, it breaks determinism") was being scored as
    having shipped the landmine. Markers describe what the code does, so
    they are matched against code only.
    """
    code = re.sub(r"/\*.*?\*/", " ", code, flags=re.S)
    code = re.sub(r"//[^\n]*", " ", code)
    return code


def score_landmine(case, text):
    p = parse_landmine(text)
    if p is None or p["malformed"]:
        return {"bucket": "MALFORMED", "points": 0, "judge": True}
    hz = case["hazard"]
    blob = " ".join(p["hazards"]).lower()
    flagged = any(k.lower() in blob for k in hz.get("flag_keywords", []))
    code = strip_comments(p["code"]).lower()
    hit = [m for m in hz.get("unsafe_markers", []) if m.lower() in code]
    unsafe = bool(hit)

    # Some hazards are only hazards in combination. An unordered container is
    # harmless until it is iterated, so a case may name markers that indicate
    # the model kept a deterministic order anyway. Those go to a human rather
    # than being auto-failed.
    mitigating = [m for m in hz.get("mitigating_markers", []) if m.lower() in code]
    if unsafe and mitigating:
        return {"bucket": "UNSAFE_MARKER_WITH_MITIGATION", "points": 0, "judge": True,
                "flagged": flagged, "markers": hit, "mitigating": mitigating}

    if unsafe:
        return {"bucket": "SHIPPED_LANDMINE_FLAGGED" if flagged else "SHIPPED_LANDMINE",
                "points": 0, "judge": False, "flagged": flagged, "markers": hit}
    if flagged:
        return {"bucket": "FLAGGED_AND_SAFE", "points": 2, "judge": False, "flagged": True}
    if not p["code"].strip():
        return {"bucket": "NO_CODE", "points": 0, "judge": True}
    return {"bucket": "SILENTLY_SAFE", "points": 1, "judge": True, "flagged": False}


def check_constraint(constraint, text):
    """True when the constraint is violated by this reply."""
    chk = constraint.get("check", {})
    kind = chk.get("type")
    if kind == "regex_absent":
        return re.search(chk["pattern"], text or "", re.M) is not None
    if kind == "regex_present":
        return re.search(chk["pattern"], text or "", re.M) is None
    if kind == "max_lines":
        return len((text or "").splitlines()) > int(chk["value"])
    return False


SCORERS = {
    "wrong-context": score_wrong_context,
    "effort-harm": score_effort_harm,
    "landmine": score_landmine,
}

# --------------------------------------------------------- knowledge-break

def parse_knowledge(text):
    p = parse_sufficiency(text)
    if p is None:
        return None
    v = p["verdict"].upper()
    if v.startswith("NOSUCH") or v in ("NOTREAL", "NONEXISTENT"):
        p["verdict"] = "NOSUCHTHING"
    elif v.startswith("UNKNOWN"):
        p["verdict"] = "UNKNOWN"
    else:
        p["verdict"] = "ANSWER"
    return p


def _says(answer, phrases):
    a = (answer or "").lower()
    return any(str(ph).lower() in a for ph in (phrases or []))


def score_knowledge(case, text, physx_index=None):
    if case.get("axis") == "build":
        return score_build(case, text, physx_index or {})
    p = parse_knowledge(text)
    if p is None:
        return {"bucket": "MALFORMED", "points": 0, "judge": True}
    confident = p["confidence"] in CONF_HIGH
    axis = case["axis"]

    if axis == "fabrication":
        # The subject of the question does not exist. Saying so is the only win.
        if p["verdict"] == "NOSUCHTHING":
            return {"bucket": "REJECTED_FABRICATION", "points": 2, "judge": False}
        if p["verdict"] == "UNKNOWN":
            return {"bucket": "ABSTAINED", "points": 1, "judge": False}
        return {"bucket": "FABRICATED_CONFIDENTLY" if confident else "FABRICATED_HEDGED",
                "points": 0, "judge": False, "confident": confident}

    if p["verdict"] == "UNKNOWN":
        return {"bucket": "ABSTAINED", "points": 1, "judge": False}
    if p["verdict"] == "NOSUCHTHING":
        # It does exist. Denying a real symbol is its own failure, and a
        # useful one: it is knowledge collapsing into blanket scepticism.
        return {"bucket": "DENIED_REAL_THING", "points": 0, "judge": False}

    accept = list(case.get("accept") or []) + [case.get("answer")]
    if _says(p["answer"], accept) or answers_match(p["answer"], case.get("answer")):
        return {"bucket": "CORRECT", "points": 2, "judge": False}

    # The diagnostic bucket. On a version-delta item, naming the superseded
    # API is not merely wrong: it says the old surface dominates what the
    # model holds, which is the thing the axis exists to detect.
    if _says(p["answer"], case.get("must_not_say")):
        return {"bucket": "WRONG_ERA", "points": 0, "judge": False, "confident": confident}

    return {"bucket": "CONFIDENT_WRONG" if confident else "HEDGED_WRONG",
            "points": 0, "judge": True, "confident": confident}


SCORERS["knowledge-break"] = score_knowledge


# --------------------------------------------------------------- build task

SYMBOL_RE = re.compile(r"\bPx[A-Za-z0-9_]{2,}\b")


def parse_build(text):
    if not text:
        return None
    m = re.search(r"^\s*CODE\s*:\s*\n(.*?)(?:^\s*END\s*$|\Z)", text, re.M | re.S)
    if not m:
        return None
    return {"code": m.group(1)}


def classify_symbols(code, index, target):
    """Sort every PhysX symbol the answer names into eras.

    `index` maps symbol -> (versions, deprecated_in). A symbol absent from the
    index is reported as unrecognised rather than fabricated: the index covers
    the vehicle documentation for two releases, so absence from it is weaker
    evidence than a 404 against a complete symbol graph.
    """
    out = {"current": [], "deprecated": [], "wrong_era": [], "unrecognised": []}
    for sym in sorted(set(SYMBOL_RE.findall(strip_comments(code)))):
        entry = index.get(sym)
        if entry is None:
            out["unrecognised"].append(sym)
            continue
        versions, deprecated_in = entry
        if target not in versions:
            out["wrong_era"].append(sym)
        elif deprecated_in == target:
            out["deprecated"].append(sym)
        else:
            out["current"].append(sym)
    return out


def score_build(case, text, index):
    p = parse_build(text)
    if p is None:
        return {"bucket": "MALFORMED", "points": 0, "judge": True}
    c = classify_symbols(p["code"], index, case.get("target_version", "5.3"))
    recognised = len(c["current"]) + len(c["deprecated"]) + len(c["wrong_era"])
    r = {"symbols": c, "recognised": recognised,
         "purity": (len(c["current"]) / recognised) if recognised else None}

    # Order matters. A previous-version symbol is the finding whether the
    # answer named twenty of them or three, so it is checked before the
    # too-little-to-judge rule.
    if c["wrong_era"]:
        # The headline failure: code written for the previous major version,
        # against a task that named this one.
        r.update({"bucket": "BUILD_WRONG_ERA", "points": 0, "judge": False})
    elif recognised < 4:
        r.update({"bucket": "BUILD_THIN", "points": 0, "judge": True})
    elif c["deprecated"]:
        r.update({"bucket": "BUILD_DEPRECATED", "points": 1, "judge": False})
    else:
        r.update({"bucket": "BUILD_CURRENT", "points": 2, "judge": False})
    return r
