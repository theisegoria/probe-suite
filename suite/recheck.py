"""Print the verification checklist for the knowledge cases.

A knowledge benchmark ages. Vendors move symbols, change availability and
rename things, so an answer key that was right when it was written quietly
becomes wrong, and the suite starts scoring correct models as failures.

This prints every case with its source URL, the line that was quoted from
it, and the date it was checked, so re-verification is a walk down a list
rather than an archaeology exercise. It does no network access of its own:
the point is that a human, or a session with the page open, confirms the
quote still appears.
"""

import datetime as dt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from suite import schema  # noqa: E402

STALE_DAYS = 120


def main():
    cases = [c for c in schema.load_all(["knowledge-break"])]
    if not cases:
        sys.exit("no knowledge cases")
    today = dt.date.today()
    stale = 0
    for c in sorted(cases, key=lambda x: x["id"]):
        s = c["source"]
        try:
            age = (today - dt.date.fromisoformat(str(s["checked"]))).days
        except ValueError:
            age = None
        mark = ""
        if age is not None and age > STALE_DAYS:
            mark = "  STALE (%d days)" % age
            stale += 1
        print("%-8s %-14s %s%s" % (c["id"], c["axis"], s["url"], mark))
        print("         expect: %s" % s.get("quote", "(no quote recorded)"))
        if c["axis"] != "fabrication":
            print("         answer: %s" % c.get("answer"))
        else:
            print("         answer: the symbol must still return 404")
        print()
    print("%d case(s), %d past %d days since checking" % (len(cases), stale, STALE_DAYS))


if __name__ == "__main__":
    main()
