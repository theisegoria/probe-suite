"""Geometry predicates over a Wavefront OBJ.

The point of asking a model to emit geometry as well as pixels is that
geometry is checkable. A torus either has Euler characteristic zero or it
does not. That is a fact, where "the picture looks like a torus" is an
opinion, and an opinion is not a benchmark.

Everything here is pure Python with no dependencies, so the harness runs
wherever the compiler does.
"""

import math
from collections import defaultdict


class MeshError(Exception):
    pass


class Mesh(object):
    def __init__(self, vertices, faces, uvs=None, face_uvs=None):
        self.v = vertices          # [(x, y, z)]
        self.f = faces             # [(i, j, k)] zero based, triangles
        self.uv = uvs or []
        self.fuv = face_uvs or []

    # ------------------------------------------------------------ parsing

    @staticmethod
    def load(path):
        v, uv, faces, fuv = [], [], [], []
        with open(path, encoding="utf-8", errors="replace") as fh:
            for lineno, raw in enumerate(fh, 1):
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                bits = line.split()
                tag = bits[0]
                if tag == "v":
                    if len(bits) < 4:
                        raise MeshError("line %d: v needs three coordinates" % lineno)
                    v.append(tuple(float(x) for x in bits[1:4]))
                elif tag == "vt":
                    if len(bits) < 3:
                        raise MeshError("line %d: vt needs two coordinates" % lineno)
                    uv.append(tuple(float(x) for x in bits[1:3]))
                elif tag == "f":
                    idx, tex = [], []
                    for tok in bits[1:]:
                        parts = tok.split("/")
                        try:
                            i = int(parts[0])
                        except ValueError:
                            raise MeshError("line %d: bad face index %r" % (lineno, tok))
                        idx.append(i - 1 if i > 0 else len(v) + i)
                        if len(parts) > 1 and parts[1]:
                            t = int(parts[1])
                            tex.append(t - 1 if t > 0 else len(uv) + t)
                    # Fan triangulate anything with more than three corners, so a
                    # quad mesh is judged on its topology rather than its format.
                    for k in range(1, len(idx) - 1):
                        faces.append((idx[0], idx[k], idx[k + 1]))
                        if len(tex) == len(idx):
                            fuv.append((tex[0], tex[k], tex[k + 1]))
        if not v:
            raise MeshError("no vertices")
        if not faces:
            raise MeshError("no faces")
        n = len(v)
        for tri in faces:
            for i in tri:
                if i < 0 or i >= n:
                    raise MeshError("face index %d out of range (%d vertices)" % (i + 1, n))
        return Mesh(v, faces, uv, fuv)

    # --------------------------------------------------------- topology

    def edges(self):
        """undirected edge -> list of (face index, directed order)"""
        e = defaultdict(list)
        for fi, (a, b, c) in enumerate(self.f):
            for (i, j) in ((a, b), (b, c), (c, a)):
                key = (i, j) if i < j else (j, i)
                e[key].append((fi, i < j))
        return e

    def euler_characteristic(self):
        return len(self.v) - len(self.edges()) + len(self.f)

    def genus(self):
        """For a closed orientable surface. Meaningless otherwise."""
        chi = self.euler_characteristic()
        return (2 - chi) / 2.0

    def boundary_edges(self):
        return [k for k, uses in self.edges().items() if len(uses) == 1]

    def nonmanifold_edges(self):
        return [k for k, uses in self.edges().items() if len(uses) > 2]

    def is_closed_manifold(self):
        return not self.boundary_edges() and not self.nonmanifold_edges()

    def is_consistently_oriented(self):
        """Each shared edge should be traversed in opposite directions."""
        for uses in self.edges().values():
            if len(uses) == 2 and uses[0][1] == uses[1][1]:
                return False
        return True

    def shells(self):
        """Connected components over face adjacency."""
        adj = defaultdict(set)
        for uses in self.edges().values():
            for a in uses:
                for b in uses:
                    if a[0] != b[0]:
                        adj[a[0]].add(b[0])
        seen, count = set(), 0
        for fi in range(len(self.f)):
            if fi in seen:
                continue
            count += 1
            stack = [fi]
            while stack:
                x = stack.pop()
                if x in seen:
                    continue
                seen.add(x)
                stack.extend(adj[x] - seen)
        return count

    # --------------------------------------------------------- geometry

    def _tri(self, fi):
        a, b, c = self.f[fi]
        return self.v[a], self.v[b], self.v[c]

    @staticmethod
    def _cross(u, w):
        return (u[1] * w[2] - u[2] * w[1],
                u[2] * w[0] - u[0] * w[2],
                u[0] * w[1] - u[1] * w[0])

    def face_area(self, fi):
        p, q, r = self._tri(fi)
        u = (q[0] - p[0], q[1] - p[1], q[2] - p[2])
        w = (r[0] - p[0], r[1] - p[1], r[2] - p[2])
        n = self._cross(u, w)
        return 0.5 * math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)

    def degenerate_faces(self, rel_tol=1e-9):
        areas = [self.face_area(i) for i in range(len(self.f))]
        biggest = max(areas) if areas else 0.0
        cutoff = max(biggest * rel_tol, 1e-14)
        return [i for i, a in enumerate(areas) if a <= cutoff]

    def signed_volume(self):
        """Divergence theorem. Positive means outward winding."""
        total = 0.0
        for fi in range(len(self.f)):
            p, q, r = self._tri(fi)
            total += (p[0] * (q[1] * r[2] - q[2] * r[1])
                      - p[1] * (q[0] * r[2] - q[2] * r[0])
                      + p[2] * (q[0] * r[1] - q[1] * r[0])) / 6.0
        return total

    def normals_outward(self):
        return self.signed_volume() > 0

    def bbox(self):
        xs = [p[0] for p in self.v]
        ys = [p[1] for p in self.v]
        zs = [p[2] for p in self.v]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

    # ------------------------------------------------------------- uvs

    def uv_stats(self):
        if not self.fuv or not self.uv:
            return None
        us = [self.uv[i][0] for tri in self.fuv for i in tri]
        vs = [self.uv[i][1] for tri in self.fuv for i in tri]
        outside = sum(1 for a in us + vs if a < -1e-6 or a > 1 + 1e-6)
        return {"count": len(self.uv), "u_range": (min(us), max(us)),
                "v_range": (min(vs), max(vs)),
                "outside_unit_square": outside,
                "faces_with_uv": len(self.fuv), "faces": len(self.f)}


def describe(path):
    m = Mesh.load(path)
    lo, hi = m.bbox()
    return {
        "vertices": len(m.v), "faces": len(m.f), "edges": len(m.edges()),
        "euler_characteristic": m.euler_characteristic(),
        "genus": m.genus(),
        "closed_manifold": m.is_closed_manifold(),
        "boundary_edges": len(m.boundary_edges()),
        "nonmanifold_edges": len(m.nonmanifold_edges()),
        "consistently_oriented": m.is_consistently_oriented(),
        "shells": m.shells(),
        "degenerate_faces": len(m.degenerate_faces()),
        "signed_volume": m.signed_volume(),
        "normals_outward": m.normals_outward(),
        "bbox_min": lo, "bbox_max": hi,
        "uv": m.uv_stats(),
    }
