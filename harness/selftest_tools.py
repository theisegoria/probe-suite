"""Tests for the parts of the harness that are not ladders.

The ladder self tests prove the scoring discriminates. Nothing proved the
supporting code works, and it is the supporting code that decides whether
a submission is even reachable: a trace predicate that measures tilt wrongly
fails everybody, and an SDK finder that gives up on a valid checkout means
the benchmark simply does not run.

    python3 harness/selftest_tools.py
"""

import contextlib
import datetime as dt
import io
import math
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from harness import fit_jeep, physx_env, trace as tr  # noqa: E402
from suite import sources  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

results = []


def check(label, got, want, tol=None):
    if tol is None:
        ok = got == want
    else:
        ok = abs(got - want) <= tol
    results.append((label, ok, got, want))
    return ok


def near(label, got, want, tol):
    return check(label, got, want, tol)


# ------------------------------------------------------------ fit_jeep

def write_obj(path, verts, faces):
    with open(path, "w", encoding="utf-8") as fh:
        for v in verts:
            fh.write("v %.6f %.6f %.6f\n" % tuple(v))
        for f in faces:
            fh.write("f %s\n" % " ".join(str(i + 1) for i in f))


def unit_box(sx, sy, sz):
    """A box, as eight corners and twelve triangles."""
    v = [(x * sx / 2, y * sy / 2, z * sz / 2)
         for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
             (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    f = []
    for q in quads:
        f.append((q[0], q[1], q[2]))
        f.append((q[0], q[2], q[3]))
    return v, f


def test_fit_jeep(tmp):
    # The shipped asset is already fitted, so fitting it again must be a
    # fixed point. If this drifts, every future re-fit drifts with it.
    verts, faces = fit_jeep.read_obj(os.path.join(ROOT, "assets", "jeep.obj"))
    out, scale, _ = fit_jeep.fit(verts, fit_jeep.DEFAULT_AXES, False)
    lo0, hi0, size0 = fit_jeep.extent(verts)
    lo1, hi1, size1 = fit_jeep.extent(out)
    near("fit is a fixed point: scale", scale, 1.0, 1e-4)
    for i, name in enumerate("xyz"):
        near("fit is a fixed point: %s extent" % name, size1[i], size0[i], 1e-4)
    near("tyres land on the ground", lo1[1], fit_jeep.GROUND_Y, 1e-4)

    # Same mesh at a silly scale and offset has to fit to the same thing.
    moved = [[v[0] * 7.3 + 40.0, v[1] * 7.3 - 12.0, v[2] * 7.3 + 5.0] for v in verts]
    out2, scale2, _ = fit_jeep.fit(moved, fit_jeep.DEFAULT_AXES, False)
    lo2, hi2, size2 = fit_jeep.extent(out2)
    near("scale and offset are removed", scale2, 1.0 / 7.3, 1e-6)
    for i, name in enumerate("xyz"):
        near("refit matches: %s extent" % name, size2[i], size0[i], 1e-3)
    near("refit lands on the ground", lo2[1], fit_jeep.GROUND_Y, 1e-4)

    # Length always ends up at the target, whatever came in.
    v3, f3 = unit_box(0.9, 0.4, 1.9)
    p = os.path.join(tmp, "box.obj")
    write_obj(p, v3, f3)
    bv, bf = fit_jeep.read_obj(p)
    check("read_obj sees every triangle", len(bf), 12)
    bout, _, _ = fit_jeep.fit(bv, fit_jeep.DEFAULT_AXES, False)
    _, _, bsize = fit_jeep.extent(bout)
    near("length is scaled to the target", bsize[2], fit_jeep.TARGET_LENGTH, 1e-6)

    # Flipping turns it round without mirroring it: a right handed mesh
    # stays right handed, so z negates and x negates with it.
    fl, _, _ = fit_jeep.fit(bv, fit_jeep.DEFAULT_AXES, True)
    check("flip reverses z", all(abs(a[2] + b[2]) < 1e-6 for a, b in zip(bout, fl)), True)
    check("flip reverses x too, so it is not a mirror",
          all(abs(a[0] + b[0]) < 1e-6 for a, b in zip(bout, fl)), True)

    # A z-up mesh fits identically once you say so.
    zup = [[v[0], v[2], v[1]] for v in bv]
    zout, _, _ = fit_jeep.fit(zup, fit_jeep.parse_axes("x,z,y"), False)
    _, _, zsize = fit_jeep.extent(zout)
    for i, name in enumerate("xyz"):
        near("axes override: %s extent" % name, zsize[i], bsize[i], 1e-6)

    # The extent guesser ranks shortest as up, longest as length.
    g = fit_jeep.guess_axes([0.4, 1.9, 0.9])
    check("guess_axes finds the up axis", g["up"], 0)
    check("guess_axes finds the length axis", g["lng"], 1)

    # Negative OBJ indices are relative to the end of the vertex list.
    p2 = os.path.join(tmp, "neg.obj")
    with open(p2, "w", encoding="utf-8") as fh:
        fh.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n")
    nv, nf = fit_jeep.read_obj(p2)
    check("negative indices resolve", nf, [(0, 1, 2)])

    # An n-gon is fanned, not dropped.
    p3 = os.path.join(tmp, "quad.obj")
    with open(p3, "w", encoding="utf-8") as fh:
        fh.write("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1/1/1 2/2/2 3/3/3 4/4/4\n")
    _, qf = fit_jeep.read_obj(p3)
    check("a quad becomes two triangles", len(qf), 2)


# ------------------------------------------------------------ physx_env

def test_physx_env(tmp):
    old_root = os.environ.get("PROBE_PHYSX_ROOT")
    old_lib = os.environ.get("PROBE_PHYSX_LIBDIR")
    try:
        os.environ["PROBE_PHYSX_ROOT"] = os.path.join(tmp, "nowhere")
        try:
            physx_env.find_root()
            check("a missing checkout raises", False, True)
        except physx_env.PhysXMissing as e:
            check("a missing checkout raises", True, True)
            check("and the message names the variable that is wrong",
                  "PROBE_PHYSX_ROOT" in str(e), True)
            check("and does not silently fall back to the bundled checkout",
                  "engines/PhysX" in str(e) or "unset" in str(e), True)

        # A checkout that exists but was never built.
        root = os.path.join(tmp, "px", "physx")
        os.makedirs(os.path.join(root, "include", "vehicle"))
        os.environ["PROBE_PHYSX_ROOT"] = root
        check("a real checkout is found", physx_env.find_root(), root)
        try:
            physx_env.find_libdir(root)
            check("an unbuilt checkout raises", False, True)
        except physx_env.PhysXMissing as e:
            check("an unbuilt checkout raises", True, True)
            check("and says it is unbuilt, not missing", "not built" in str(e), True)

        # Two builds present: release wins over debug.
        for flavour in ("debug", "release"):
            d = os.path.join(root, "bin", "mac.arm64", flavour)
            os.makedirs(d)
            open(os.path.join(d, "libPhysXVehicle_static_64.a"), "w").close()
        check("release is preferred over debug",
              physx_env.find_libdir(root).endswith("release"), True)

        os.makedirs(os.path.join(root, "snippets", "snippetvehiclecommon", "base"))
        os.makedirs(os.path.join(root, "snippets", "snippetvehiclecommon", "enginedrivetrain"))
        os.makedirs(os.path.join(root, "snippets", "snippetvehiclecommon", "physxintegration"))
        for s in physx_env.SNIPPET_SOURCES:
            open(os.path.join(root, "snippets", "snippetvehiclecommon", s), "w").close()
        d = physx_env.describe()
        check("the static lib macro is defined",
              "-DPX_PHYSX_STATIC_LIB" in d["cxxflags"], True)
        check("the include path is passed",
              any(f.endswith("include") for f in d["cxxflags"]), True)
        check("all seven libraries are linked",
              sum(1 for f in d["ldflags"] if f.startswith("-lPhysX")), 7)
        check("extensions links before the core, as the linker needs",
              d["ldflags"].index("-lPhysXExtensions_static_64")
              < d["ldflags"].index("-lPhysXFoundation_static_64"), True)
        check("all three snippet sources are compiled in", len(d["sources"]), 3)
    finally:
        for k, v in (("PROBE_PHYSX_ROOT", old_root), ("PROBE_PHYSX_LIBDIR", old_lib)):
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


# ------------------------------------------------------------ trace

HEADER = "t,px,py,pz,qx,qy,qz,qw,speed,wheels_grounded,throttle,brake,steer\n"


def row(t, px=0.0, py=0.9, pz=0.0, q=(0, 0, 0, 1), speed=0.0, g=4):
    return "%.4f,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%.4f,%d,0,0,0\n" % (
        t, px, py, pz, q[0], q[1], q[2], q[3], speed, g)


MIN_ROWS = 10          # trace.py refuses anything shorter, so pad to it


def write_trace(path, rows):
    """Write a trace, padded to the minimum length trace.py accepts.

    Padding repeats the last row at later timestamps, which changes nothing
    the predicates under test measure: tilt, drift and bounds are already at
    their extremes, and a repeated position adds no distance."""
    rows = list(rows)
    while len(rows) < MIN_ROWS:
        last = rows[-1]
        fields = last.split(",")
        fields[0] = "%.4f" % (float(fields[0]) + 0.1)
        rows.append(",".join(fields))
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(HEADER)
        for r in rows:
            fh.write(r)


def test_trace(tmp):
    # Upright is zero tilt; rolled a quarter turn about z is ninety degrees.
    p = os.path.join(tmp, "t1.csv")
    roll90 = (0.0, 0.0, math.sin(math.pi / 4), math.cos(math.pi / 4))
    write_trace(p, [row(0.0), row(0.1, q=roll90)])
    t = tr.Trace.load(p)
    near("upright reads as no tilt", t.tilt_deg(t.rows[0]), 0.0, 1e-6)
    near("a quarter roll reads as ninety degrees", t.tilt_deg(t.rows[1]), 90.0, 1e-4)

    # Upside down is 180, which must not wrap round to 0.
    flip = (0.0, 0.0, 1.0, 0.0)
    p = os.path.join(tmp, "t2.csv")
    write_trace(p, [row(0.0), row(0.1, q=flip)])
    near("upside down reads as one eighty", tr.Trace.load(p).max_tilt(), 180.0, 1e-4)

    # Drift is measured from the first row of the window, in three dimensions.
    p = os.path.join(tmp, "t3.csv")
    write_trace(p, [row(0.0, py=1.0), row(1.0, py=0.7), row(3.0, py=0.0)])
    near("rest drift ignores rows past the window",
         tr.Trace.load(p).rest_drift(2.0), 0.3, 1e-6)
    near("rest drift over the whole run sees the lot",
         tr.Trace.load(p).rest_drift(10.0), 1.0, 1e-6)

    # Waypoints must be reached in order. A trace that drives past the
    # second one first has still only reached one.
    wps = [{"x": 0.0, "z": 10.0, "radius": 2.0}, {"x": 0.0, "z": 4.0, "radius": 2.0}]
    p = os.path.join(tmp, "t4.csv")
    write_trace(p, [row(0.0, pz=0.0), row(1.0, pz=4.0), row(2.0, pz=10.0)])
    hits = tr.Trace.load(p).waypoint_progress(wps)
    check("out of order does not count", [h is not None for h in hits], [True, False])

    # And in order it does.
    wps2 = [{"x": 0.0, "z": 4.0, "radius": 2.0}, {"x": 0.0, "z": 10.0, "radius": 2.0}]
    hits2 = tr.Trace.load(p).waypoint_progress(wps2)
    check("in order counts both", [h is not None for h in hits2], [True, True])
    near("and reports when, not just whether", hits2[0], 1.0, 1e-6)

    # Bounds are checked on all three axes.
    b = {"xmin": -1.0, "xmax": 1.0, "zmin": -1.0, "zmax": 20.0, "ymin": 0.0, "ymax": 5.0}
    p = os.path.join(tmp, "t5.csv")
    write_trace(p, [row(0.0), row(1.0, px=3.0)])
    check("leaving sideways is caught", tr.Trace.load(p).out_of_bounds(b) is not None, True)
    p = os.path.join(tmp, "t6.csv")
    write_trace(p, [row(0.0), row(1.0, py=-2.0)])
    check("falling through is caught", tr.Trace.load(p).out_of_bounds(b) is not None, True)
    p = os.path.join(tmp, "t7.csv")
    write_trace(p, [row(0.0), row(1.0, pz=5.0)])
    check("staying inside is not", tr.Trace.load(p).out_of_bounds(b), None)

    # A trace that is malformed has to say so rather than score as zero.
    for label, body in [
        ("a missing column is rejected", "t,px,py\n0,0,0\n1,0,0\n"),
        ("a non numeric cell is rejected",
         HEADER + "0,0,0,0,0,0,0,1,0,4,0,0,0\n1,x,0,0,0,0,0,1,0,4,0,0,0\n"),
        ("a trace too short to mean anything is rejected",
         HEADER + "".join("%.1f,0,0,0,0,0,0,1,0,4,0,0,0\n" % (i * 0.1) for i in range(4))),
    ]:
        p = os.path.join(tmp, "bad.csv")
        with open(p, "w", encoding="utf-8") as fh:
            fh.write(body)
        try:
            tr.Trace.load(p)
            check(label, False, True)
        except tr.TraceError:
            check(label, True, True)


# ------------------------------------------------------------ freshness

def quietly(fn, *a):
    """report_freshness talks to the operator. Here we only want its verdict."""
    with contextlib.redirect_stdout(io.StringIO()):
        return fn(*a)


def test_freshness(tmp):
    """The snapshot clock has to actually tick, or it protects nothing."""
    today = dt.date.today()
    states = {d: s for d, _, s in sources.freshness(today)}
    check("shipped snapshots are current", set(states.values()), {"ok"})

    warn = today + dt.timedelta(days=sources.WARN_DAYS + 1)
    states = {d: s for d, _, s in sources.freshness(warn)}
    check("past the warning age they warn", set(states.values()), {"warn"})
    check("a warning is not a failure", quietly(sources.report_freshness, warn), 0)

    fail = today + dt.timedelta(days=sources.FAIL_DAYS + 1)
    states = {d: s for d, _, s in sources.freshness(fail)}
    check("past the limit they are stale", set(states.values()), {"stale"})
    check("and staleness fails", quietly(sources.report_freshness, fail), len(states))

    old = os.environ.get(sources.OVERRIDE)
    try:
        os.environ[sources.OVERRIDE] = "1"
        check("unless you say you have checked by hand",
              quietly(sources.report_freshness, fail), 0)
    finally:
        if old is None:
            os.environ.pop(sources.OVERRIDE, None)
        else:
            os.environ[sources.OVERRIDE] = old


def main():
    tmp = tempfile.mkdtemp(prefix="probe-tools-")
    print("\nharness tools: the code the ladders stand on\n")
    try:
        test_fit_jeep(tmp)
        test_physx_env(tmp)
        test_trace(tmp)
        test_freshness(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    failed = [r for r in results if not r[1]]
    for label, ok, got, want in results:
        if not ok:
            print("  FAIL  %-46s got %r, wanted %r" % (label, got, want))
    print("  %d check(s) over fit_jeep, physx_env, trace and source freshness"
          % len(results))
    print("\n%d check(s), %d failure(s)" % (len(results), len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
