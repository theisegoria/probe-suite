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
