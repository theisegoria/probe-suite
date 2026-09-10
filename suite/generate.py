"""Build an item set from a source snapshot.

The published artifact is this generator and its templates, not an answer
key. Each run draws its own items and takes every answer from the source,
so publishing the suite does not destroy it: a model can memorise the
vendor documentation, and memorising the vendor documentation is exactly
the knowledge under test.

Generation is seeded, so a run can be reproduced exactly when you need to
compare like with like, and varied when you do not.
"""

import argparse
import datetime as dt
import io
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from suite import sources  # noqa: E402

ROOT = sources.ROOT
OUT = os.path.join(ROOT, "generated")


def _src(url, quote, checked):
    return {"url": url, "checked": checked, "quote": quote}


# ------------------------------------------------------------------ vulkan

def gen_vulkan(src, rng, n):
    items = []
    checked = src["checked"]
    appendix = src["sources"]["versions_appendix"]
    promoted = src["promoted_to_core"]
    pairs = [(e, v) for v, exts in promoted.items() for e in exts]
    rng.shuffle(pairs)

    for ext, ver in pairs:
        other = "1.3" if ver == "1.4" else "1.4"
        items.append(dict(
            axis="version-delta", domain="vulkan", introduced="Vulkan %s core" % ver,
            question="Which core version of Vulkan promoted %s to core? Answer with the "
                     "version number alone." % ext,
            answer=ver, accept=[ver],
            must_not_say=[other],
            source=_src(appendix, "%s is listed under extensions promoted to core in "
                                  "version %s" % (ext, ver), checked)))

    # A model that knows the extension but not that it is core will still tell
    # you to enable it. The answer is a plain no, and the failure is expensive:
    # enabling a promoted extension on a 1.4 device is a silent portability bug.
    for ext, ver in pairs:
        items.append(dict(
            axis="version-delta", domain="vulkan", introduced="Vulkan %s core" % ver,
            question="On a Vulkan %s device, do you still need to enable the %s extension "
                     "to use its functionality? Answer yes or no." % (ver, ext),
            answer="no", accept=["no"], must_not_say=["yes"],
            source=_src(appendix, "%s was promoted to core in %s" % (ext, ver), checked)))

    cmds = src["new_commands"]["1.4"]
    structs = src["new_structures"]["1.4"]
    for name in cmds + structs:
        kind = "command" if name.startswith("vk") else "structure"
        items.append(dict(
            axis="coverage", domain="vulkan", introduced="Vulkan 1.4 core",
            question="Was the %s %s added to core Vulkan in version 1.4, or does it "
                     "predate 1.4? Answer 1.4 or earlier." % (kind, name),
            answer="1.4", accept=["1.4"], must_not_say=["earlier", "1.3", "1.2"],
            source=_src(appendix, "%s is listed among the new %ss in version 1.4"
                        % (name, kind), checked)))

    rng.shuffle(items)
    return items[:n]


# ------------------------------------------------------------------- physx

BUILD_TASKS = [
    ("a four-wheeled vehicle with engine drive",
     "Set up a four-wheeled vehicle with engine drive in PhysX {ver}. Give the "
     "parameter and state types you would populate, the ordered set of components "
     "you would run, and the call that advances the simulation."),
    ("a direct-drive vehicle",
     "Set up a direct-drive four-wheeled vehicle in PhysX {ver}, the kind with no "
     "gearbox or clutch. Name the types you would populate and the components you "
     "would run, in order."),
    ("a tank",
     "Set up a tracked vehicle with tank-style differential steering in PhysX {ver}. "
     "Name the differential type, the command state type, and the components involved."),
    ("suspension and tire tuning",
     "In PhysX {ver}, which types hold suspension geometry, suspension force response, "
     "and tire force response for a wheel, and which components consume them?"),
    ("attaching the vehicle to the scene",
     "In PhysX {ver}, how do you create and configure the PhysX actor backing a "
     "vehicle, and which components bridge the vehicle to the scene each step?"),
]


def gen_physx(src, rng, n):
    items = []
    checked = src["checked"]
    url = src["sources"]["5.3"]
    for label, tmpl in BUILD_TASKS:
        items.append(dict(
            axis="build", domain="physx", target_version="5.3",
            title=label,
            question=tmpl.format(ver="5.3"),
            source=_src(url, "PhysX 5.3 vehicle documentation, component architecture "
                             "introduced in 5.1", checked)))
    rng.shuffle(items)
    return items[:n]


# ------------------------------------------------------------------- metal

def gen_metal(src, rng, n):
    items = []
    checked = src["checked"]
    for sym in src["symbols"]:
        base = dict(domain="metal", introduced=sym.get("introduced"),
                    source=_src(sym["url"], sym.get("quote", ""), checked))
        if sym.get("exists") is False:
            items.append(dict(base, axis="fabrication", exists=False,
                              question=sym["question"]))
            continue
        if sym.get("min_macos"):
            items.append(dict(base, axis="recency",
                              question="From which macOS version has the %s %s been "
                                       "available?" % (sym["name"], sym["kind"]),
                              answer=sym["min_macos"], accept=[sym["min_macos"]]))
        for m in sym.get("members", []):
            items.append(dict(base, axis="coverage",
                              question="Give the full name, including argument labels, of "
                                       "the %s method that %s." % (sym["name"], m["does"]),
                              answer=m["name"], accept=[m["name"].split("(")[0].lower()]))
    rng.shuffle(items)
    return items[:n]


GENERATORS = {"vulkan": gen_vulkan, "physx": gen_physx, "metal": gen_metal}


def main():
    ap = argparse.ArgumentParser(description="Generate an item set from a source snapshot.")
    ap.add_argument("--domain", action="append", default=[],
                    choices=sorted(GENERATORS), help="Repeatable. Defaults to all cached.")
    ap.add_argument("--n", type=int, default=20, help="Items per domain")
    ap.add_argument("--seed", type=int, default=None,
                    help="Reproduce an earlier set exactly. Omit for a fresh draw.")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    seed = a.seed if a.seed is not None else random.randrange(1, 10 ** 6)
    rng = random.Random(seed)
    domains = a.domain or [d for d in sources.available() if d in GENERATORS]

    items, meta = [], {}
    for d in domains:
        src = sources.load(d)
        got = GENERATORS[d](src, rng, a.n)
        for i, it in enumerate(got, 1):
            it["id"] = "%s-%03d" % (d[:2], i)
            it["instrument"] = "knowledge-break"
        meta[d] = {"checked": src["checked"], "items": len(got)}
        items += got

    tag = "gen-%s-s%d" % ("-".join(domains), seed)
    os.makedirs(OUT, exist_ok=True)
    path = a.out or os.path.join(OUT, tag + ".json")
    io.open(path, "w", encoding="utf-8").write(json.dumps(
        {"tag": tag, "seed": seed, "generated": dt.datetime.now().isoformat(timespec="seconds"),
         "sources": meta, "items": items}, indent=1))

    print("%d item(s) -> %s" % (len(items), os.path.relpath(path, ROOT)))
    print("seed %d  (pass --seed %d to draw exactly this set again)" % (seed, seed))
    for d, m in meta.items():
        print("  %-8s %2d items from a source checked %s" % (d, m["items"], m["checked"]))


if __name__ == "__main__":
    main()
