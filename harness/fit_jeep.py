"""Normalise a downloaded jeep mesh into the asset the benchmark ships.

The jeep is the one asset that may come from outside: the physics reads the
dimensions out of the task prompt, not out of the file, so the mesh only has
to look like the vehicle the task describes and sit in the same frame as it.
This makes that true of any OBJ, and says what it did.

  python3 harness/fit_jeep.py downloaded.obj            # inspect and report
  python3 harness/fit_jeep.py downloaded.obj --write    # write assets/jeep.obj

The target frame, which is also the frame the reference solution uses:

  +z forward, +x lateral, +y up
  origin at the centre of mass
  length 3.80 over the whole vehicle
  the tyres touch y = -0.88, which is the wheel centre 0.52 below the origin
  less the 0.36 radius, so the mesh looks planted rather than hovering

The collision box the physics uses is 1.80 by 0.80 by 3.80 and is not read
from this file, so the mesh only has to look like that vehicle, not measure
as it. What it does have to do is sit in the same frame.
"""

import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TARGET_LENGTH = 3.80
GROUND_Y = -0.88                 # wheel centre 0.52 below the origin, radius 0.36
# What a whole vehicle, wheels and roof included, ought to measure. Wider or
# taller than this and the picture stops matching the collision box.
WIDTH_BAND = (1.70, 2.25)
HEIGHT_BAND = (1.40, 2.20)


def read_obj(path):
    """Vertices and faces. Normals and texture coordinates are dropped: the
    asset is a shape, and carrying a material that points at a texture file
    we do not ship would only produce a broken reference."""
    verts, faces = [], []
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("v "):
            p = line.split()
            verts.append([float(p[1]), float(p[2]), float(p[3])])
        elif line.startswith("f "):
            idx = []
            for tok in line.split()[1:]:
                i = int(tok.split("/")[0])
                idx.append(i - 1 if i > 0 else len(verts) + i)
            for k in range(1, len(idx) - 1):
                faces.append((idx[0], idx[k], idx[k + 1]))
    if not verts or not faces:
        raise SystemExit("%s has no geometry this reader understands" % path)
    return verts, faces


def extent(verts):
    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    return lo, hi, [hi[i] - lo[i] for i in range(3)]


DEFAULT_AXES = {"lat": 0, "up": 1, "lng": 2}      # the OBJ convention: y up, -z forward


def guess_axes(size):
    """Rank the extents: longest is the length, shortest is the height. Only
    used with --guess, because it is wrong for any vehicle that is taller
    than it is wide, which an open top with a roll bar can easily be."""
    order = sorted(range(3), key=lambda i: size[i])
    return {"up": order[0], "lat": order[1], "lng": order[2]}


def parse_axes(spec):
    """--axes lat,up,lng as source axis letters, e.g. x,y,z or x,z,y."""
    letters = [s.strip().lower() for s in spec.split(",")]
    if len(letters) != 3 or any(c not in "xyz" for c in letters):
        raise SystemExit("--axes wants three of x, y, z, as lat,up,lng")
    n = {"x": 0, "y": 1, "z": 2}
    return {"lat": n[letters[0]], "up": n[letters[1]], "lng": n[letters[2]]}


def fit(verts, axes, flip_lng):
    lo, hi, size = extent(verts)
    src = (axes["lat"], axes["up"], axes["lng"])
    have = [size[a] for a in src]
    if min(have) <= 0:
        raise SystemExit("the mesh is flat on one axis, nothing to fit")

    # Uniform, driven by the length, so the vehicle keeps its proportions.
    # Anything else would make a jeep that is right by the tape and wrong to
    # the eye, and the eye is the only thing this asset serves.
    s = TARGET_LENGTH / have[2]

    mid = [(lo[a] + hi[a]) / 2.0 for a in src]
    out = []
    for v in verts:
        x = (v[src[0]] - mid[0]) * s
        y = (v[src[1]] - mid[1]) * s
        z = (v[src[2]] - mid[2]) * s
        if flip_lng:
            z = -z
            x = -x                      # turn it round without mirroring it
        out.append([x, y, z])
    # Drop it onto its wheels rather than centring it on the origin.
    low = min(v[1] for v in out)
    for v in out:
        v[1] += GROUND_Y - low
    return out, s, have


def write_obj(path, verts, faces, note):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("# probe suite jeep\n")
        for line in note:
            fh.write("# %s\n" % line)
        fh.write("# origin at the centre of mass, +z forward, +y up\n")
        for v in verts:
            fh.write("v %.4f %.4f %.4f\n" % tuple(v))
        for f in faces:
            fh.write("f %d %d %d\n" % (f[0] + 1, f[1] + 1, f[2] + 1))


def main():
    ap = argparse.ArgumentParser(description="Fit a downloaded mesh to the jeep frame.")
    ap.add_argument("source")
    ap.add_argument("--axes", default=None,
                    help="source axes as lat,up,lng. Defaults to x,y,z, the OBJ convention")
    ap.add_argument("--guess", action="store_true",
                    help="infer the axes by ranking the extents instead of assuming x,y,z")
    ap.add_argument("--flip", action="store_true", help="the model faces -z, turn it round")
    ap.add_argument("--write", action="store_true", help="write assets/jeep.obj")
    ap.add_argument("--out", default=os.path.join(ROOT, "assets", "jeep.obj"))
    a = ap.parse_args()

    verts, faces = read_obj(a.source)
    lo, hi, size = extent(verts)
    print("\n  source        %s" % a.source)
    print("  triangles     %d" % len(faces))
    print("  vertices      %d" % len(verts))
    print("  extents       x %.3f  y %.3f  z %.3f" % tuple(size))

    if a.axes:
        axes, how = parse_axes(a.axes), ""
    elif a.guess:
        axes, how = guess_axes(size), "   (guessed by ranking the extents)"
    else:
        axes, how = DEFAULT_AXES, "   (the OBJ default; --axes or --guess to change it)"
    print("  axes          lateral %s, up %s, longitudinal %s%s"
          % ("xyz"[axes["lat"]], "xyz"[axes["up"]], "xyz"[axes["lng"]], how))

    out, scale, have = fit(verts, axes, a.flip)
    lo2, hi2, size2 = extent(out)
    print("  scaled by     %.4f (uniform, on length)" % scale)
    print("  fitted        width %.3f  height %.3f  length %.3f" % tuple(size2))
    print("  y span        %.3f to %.3f   (tyres should touch %.2f)"
          % (lo2[1], hi2[1], GROUND_Y))

    warn = []
    if not (WIDTH_BAND[0] <= size2[0] <= WIDTH_BAND[1]):
        warn.append("width %.2f is outside %.2f to %.2f" % (size2[0], WIDTH_BAND[0], WIDTH_BAND[1]))
    if not (HEIGHT_BAND[0] <= size2[1] <= HEIGHT_BAND[1]):
        warn.append("height %.2f is outside %.2f to %.2f" % (size2[1], HEIGHT_BAND[0], HEIGHT_BAND[1]))
    if warn:
        print("\n  the fitted vehicle does not match the collision box the physics uses:")
        for w in warn:
            print("    %s" % w)
        print("  the simulation will not care, the picture will. Check --axes: a wrong "
              "guess above is the usual cause.")

    if a.write:
        note = ["fitted from %s by harness/fit_jeep.py" % os.path.basename(a.source),
                "scaled %.4f uniformly on length, tyres dropped to y = %.2f"
                % (scale, GROUND_Y)]
        write_obj(a.out, out, faces, note)
        print("\n  wrote %s  (%d triangles, %.0f KB)"
              % (os.path.relpath(a.out, ROOT), len(faces), os.path.getsize(a.out) / 1024.0))
    else:
        print("\n  nothing written. Add --write when the numbers above look right.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
