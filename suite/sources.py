"""Ground-truth sources.

Each domain has a vendor-published, versioned source of truth. The suite
caches a dated snapshot of each so it runs offline, and refreshes from the
live source on request. The cache is a copy of public vendor documentation,
not a curated answer key: the questions are drawn from it at generation
time, which is what keeps the instrument publishable.

Refreshing needs network access. It is deliberately a separate step from
running, so a run never silently changes its own ground truth halfway
through a comparison.
"""

import datetime as dt
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, "sources")

REFRESH = {
    "vulkan": [
        ("promoted_to_core, new_commands, new_structures",
         "https://docs.vulkan.org/spec/latest/appendices/versions.html"),
        ("full registry, if you want more than the appendix carries",
         "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/xml/vk.xml"),
    ],
    "physx": [
        ("4.1 vehicle symbols",
         "https://nvidiagameworks.github.io/PhysX/4.1/documentation/physxguide/Manual/Vehicles.html"),
        ("5.3 vehicle symbols and the deprecation list",
         "https://nvidia-omniverse.github.io/PhysX/physx/5.3.1/docs/Vehicles.html"),
    ],
    "metal": [
        ("any symbol, exact members and per-platform availability",
         "https://developer.apple.com/tutorials/data/documentation/metal/<symbol>.json"),
    ],
}


def load(domain):
    path = os.path.join(DIR, "%s.json" % domain)
    if not os.path.exists(path):
        sys.exit("no cached source for %r. See sources/SOURCES.md" % domain)
    with io.open(path, encoding="utf-8") as fh:
        return json.load(fh)


def available():
    if not os.path.isdir(DIR):
        return []
    return sorted(f[:-5] for f in os.listdir(DIR) if f.endswith(".json"))


# A snapshot is a dated copy of vendor documentation, so it rots. Past the
# warning age the suite says so; past the failing age the self test refuses
# to pass, because a stale snapshot does not make the benchmark noisier, it
# makes it wrong in a specific direction: current API that the vendor added
# after the snapshot gets scored as fabrication, and the model is marked
# down for being more right than the harness.
WARN_DAYS = 90
FAIL_DAYS = 180
OVERRIDE = "PROBE_ALLOW_STALE_SOURCES"


def age_days(domain, today=None):
    """Days since this snapshot was checked, or None if it does not say."""
    checked = load(domain).get("checked")
    if not checked:
        return None
    try:
        when = dt.date.fromisoformat(str(checked))
    except ValueError:
        return None
    return ((today or dt.date.today()) - when).days


def freshness(today=None):
    """(domain, age, state) per snapshot. state is ok, warn, stale or unknown."""
    out = []
    for d in available():
        age = age_days(d, today)
        if age is None:
            state = "unknown"
        elif age >= FAIL_DAYS:
            state = "stale"
        elif age >= WARN_DAYS:
            state = "warn"
        else:
            state = "ok"
        out.append((d, age, state))
    return out


def report_freshness(today=None):
    """Print the state of every snapshot. Returns the number that are stale.

    Set PROBE_ALLOW_STALE_SOURCES=1 to be told and not stopped, which is the
    right setting when you have checked by hand that the vendor has not moved.
    """
    rows = freshness(today)
    bad = 0
    for domain, age, state in rows:
        if state == "ok":
            continue
        if state == "unknown":
            print("  sources/%s.json has no `checked` date, so its age is unknown" % domain)
            bad += 1
        elif state == "warn":
            print("  sources/%s.json was checked %d days ago. Past %d it will fail: "
                  "python3 suite/sources.py" % (domain, age, FAIL_DAYS))
        else:
            print("  sources/%s.json was checked %d days ago, over the %d day limit. "
                  "Refresh it: python3 suite/sources.py" % (domain, age, FAIL_DAYS))
            bad += 1
    if bad and os.environ.get(OVERRIDE):
        print("  %s is set, so a stale snapshot is a warning here rather than a failure."
              % OVERRIDE)
        return 0
    return bad


def physx_index(src):
    """symbol -> (versions, deprecated_in)."""
    return {k: (tuple(v["versions"]), v.get("deprecated_in"))
            for k, v in src["symbols"].items()}


def main():
    for d in available():
        s = load(d)
        print("%-8s checked %s" % (d, s.get("checked")))
        for label, url in REFRESH.get(d, []):
            print("         %s\n           %s" % (label, url))
        print()
    print("These snapshots are copies of vendor documentation. To refresh one,")
    print("re-read the URLs above and rewrite the JSON, then bump `checked`.")
    print("suite/recheck.py lists what each cached fact is supposed to say.")


if __name__ == "__main__":
    main()
