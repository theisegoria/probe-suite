"""Predicates over a vehicle trajectory.

The program writes one row per simulation tick. Everything the benchmark
judges is derived from that file, so the grading is the same whether the
model rendered anything or not, and a run can be re-scored later without
re-running the physics.

Tilt is measured as the angle between the vehicle's own up axis and world
up, rather than as roll and pitch separately. That is rotation order
independent, which matters when a vehicle is part way through leaving the
ground.
"""

import csv
import math

REQUIRED = ["t", "px", "py", "pz", "qx", "qy", "qz", "qw"]


class TraceError(Exception):
    pass


class Trace(object):
    def __init__(self, rows):
        self.rows = rows

    @staticmethod
    def load(path):
        with open(path, newline="", encoding="utf-8", errors="replace") as fh:
            rd = csv.DictReader(fh)
            if rd.fieldnames is None:
                raise TraceError("empty file")
            names = [f.strip() for f in rd.fieldnames]
            missing = [c for c in REQUIRED if c not in names]
            if missing:
                raise TraceError("missing column(s): %s. Found: %s"
                                 % (", ".join(missing), ", ".join(names)))
            rows = []
            for i, raw in enumerate(rd, 2):
                row = {}
                for c in names:
                    v = (raw.get(c) or "").strip()
                    if c in REQUIRED or v not in ("", None):
                        try:
                            row[c] = float(v)
                        except ValueError:
                            if c in REQUIRED:
                                raise TraceError("line %d: column %s is %r, not a number"
                                                 % (i, c, v))
                            continue
                for c in REQUIRED:
                    x = row.get(c)
                    if x is None or math.isnan(x) or math.isinf(x):
                        raise TraceError("line %d: column %s is %r" % (i, c, x))
                rows.append(row)
        if len(rows) < 10:
            raise TraceError("only %d row(s) of trajectory" % len(rows))
        return Trace(rows)

    # -------------------------------------------------------- kinematics

    @staticmethod
    def up_axis(r):
        """The vehicle's own +y axis, rotated into world space."""
        x, y, z, w = r["qx"], r["qy"], r["qz"], r["qw"]
        n = math.sqrt(x * x + y * y + z * z + w * w)
        if n < 1e-9:
            return (0.0, 1.0, 0.0)
        x, y, z, w = x / n, y / n, z / n, w / n
        return (2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x))

    @staticmethod
    def forward_axis(r):
        x, y, z, w = r["qx"], r["qy"], r["qz"], r["qw"]
        n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
        x, y, z, w = x / n, y / n, z / n, w / n
        return (1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y))

    def tilt_deg(self, r):
        up = self.up_axis(r)
        return math.degrees(math.acos(max(-1.0, min(1.0, up[1]))))

    def max_tilt(self, t0=None, t1=None):
        vals = [self.tilt_deg(r) for r in self.window(t0, t1)]
        return max(vals) if vals else 0.0

    def window(self, t0=None, t1=None):
        return [r for r in self.rows
                if (t0 is None or r["t"] >= t0) and (t1 is None or r["t"] <= t1)]

    def duration(self):
        return self.rows[-1]["t"] - self.rows[0]["t"]

    def distance_travelled(self):
        d = 0.0
        for a, b in zip(self.rows, self.rows[1:]):
            d += math.dist((a["px"], a["pz"]), (b["px"], b["pz"]))
        return d

    def max_speed(self):
        best = 0.0
        for a, b in zip(self.rows, self.rows[1:]):
            dt = b["t"] - a["t"]
            if dt > 1e-6:
                best = max(best, math.dist((a["px"], a["py"], a["pz"]),
                                           (b["px"], b["py"], b["pz"])) / dt)
        return best

    def heading_change_deg(self):
        """Total absolute yaw swing, which is how you tell steering works."""
        total = 0.0
        prev = None
        for r in self.rows:
            f = self.forward_axis(r)
            yaw = math.degrees(math.atan2(f[2], f[0]))
            if prev is not None:
                d = (yaw - prev + 180.0) % 360.0 - 180.0
                total += abs(d)
            prev = yaw
        return total

    def rest_drift(self, t1):
        w = self.window(None, t1)
        if not w:
            return 0.0
        a = w[0]
        return max(math.dist((a["px"], a["py"], a["pz"]), (r["px"], r["py"], r["pz"])) for r in w)

    def waypoint_progress(self, waypoints):
        """First tick within radius of each waypoint, in order. None if never."""
        hits, idx = [], 0
        for wp in waypoints:
            found = None
            while idx < len(self.rows):
                r = self.rows[idx]
                if math.dist((r["px"], r["pz"]), (wp["x"], wp["z"])) <= wp.get("radius", 3.0):
                    found = r["t"]
                    break
                idx += 1
            hits.append(found)
            if found is None:
                break
        return hits

    def out_of_bounds(self, bounds):
        for r in self.rows:
            if not (bounds["xmin"] <= r["px"] <= bounds["xmax"]):
                return r
            if not (bounds["zmin"] <= r["pz"] <= bounds["zmax"]):
                return r
            if not (bounds["ymin"] <= r["py"] <= bounds["ymax"]):
                return r
        return None


def describe(path):
    tr = Trace.load(path)
    first, last = tr.rows[0], tr.rows[-1]
    return {
        "rows": len(tr.rows), "duration": tr.duration(),
        "start": (first["px"], first["py"], first["pz"]),
        "end": (last["px"], last["py"], last["pz"]),
        "distance": tr.distance_travelled(),
        "max_speed": tr.max_speed(),
        "max_tilt_deg": tr.max_tilt(),
        "heading_change_deg": tr.heading_change_deg(),
    }
