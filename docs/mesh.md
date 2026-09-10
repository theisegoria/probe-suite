[Back to the suite](../README.md)

# Mesh and render: can the model build the thing, not just describe it

The model writes one self-contained C++23 file. When run, it must generate a
mesh procedurally, write it as an OBJ, and rasterise it to an image with a
renderer it also wrote. No libraries beyond the standard library, so there
is nowhere to hide: no mesh package, no image package, no GPU.

Then it iterates. Whatever failed comes back as structured text plus the
frame it rendered, and it tries again. What gets recorded is the rung
reached at each iteration.

## Why geometry, not just pixels

Because geometry is checkable and pictures are arguable. A torus either has
Euler characteristic zero or it does not. Asking for the OBJ as well as the
image turns "it looks about right" into arithmetic:

| check | what it catches |
|---|---|
| Euler characteristic, genus | a sphere submitted as a torus, a seam that closed wrongly |
| every edge used by exactly two faces | an open surface pretending to be solid |
| consistent winding across shared edges | a mesh that renders but cannot be shaded or simulated |
| signed volume sign | normals inward, the classic silent failure |
| degenerate triangle count | pole collapse, duplicated vertices |
| shell count | a mesh accidentally built as two loose halves |
| bounding box | right shape, wrong size |

The image predicates are chosen so each fails for its own reason: coverage
catches an empty or a full frame, shading spread catches a silhouette that
is filled but never lit, background component count catches a torus drawn
as a disc, and silhouette mirror catches a shape that should be symmetric
and is not.

## The ladder

Rungs are strictly ordered, so the number says which mistake was made:

```
1  compiles
2  runs without crashing
3  emits parseable geometry
4  geometry is a closed 2-manifold
5  topology matches the spec
6  renders a non-degenerate image
7  image predicates hold
```

A model that reaches 4 and never 5 is making a different mistake from one
that never reaches 2, and a pass rate hides that.

## Verified, not assumed

```
python3 harness/selftest_mesh.py
```

It takes the reference solution and breaks it in one specific way per rung,
then asserts each mutant stops exactly where it should:

```
ok    unmodified reference               rung 7 (expected 7)
ok    does not compile                   rung 0 (expected 0)
ok    compiles but exits non zero        rung 1 (expected 1)
ok    writes no geometry                 rung 2 (expected 2)
ok    open surface, not closed           rung 3 (expected 3)
ok    closed but normals inward          rung 4 (expected 4)
ok    geometry right, renders nothing    rung 5 (expected 5)
ok    renders a flat unlit silhouette    rung 6 (expected 6)
```

A ladder that reported 7 for everything would look identical to a working
one in a summary table. This is what separates them.

Mutants only test rejection. A ladder tuned so tightly around one solution
that it fails every model would pass the table above and still be useless,
so the self-test also runs a second reference written to share as little as
possible with the first:

```
ok    second reference, swept and painted   rung 7 (expected 7)
```

`mr-01b.cpp` builds the torus by sweeping a circle on an explicit frame
rather than from the closed form, at 40 by 29 instead of 48 by 24, stores
it v major instead of u major, splits each quad on the other diagonal, and
renders with a painter's algorithm and no depth buffer, from a different
camera under a different light. It reached rung 7 on its first run.

It is also an independent witness for the predicate that was fixed: it
scores 0.77 on the old luminance mirror, which would have failed the old
check, and 0.999 on the silhouette mirror that replaced it.

The reference also earned its keep before any model saw the task. Its first
run reached rung 6, failing a left to right symmetry check, and the renderer
was right: the predicate measured luminance symmetry, which an off-axis
light legitimately breaks. The check now measures the silhouette against its
own mirror, which is what the task actually asks for. The reference is there
to catch unsatisfiable tasks, and it caught one.

## Running it

```
./probe mesh selftest                                 # prove the ladder discriminates
python3 suite/mesh_loop.py --model paste --task mr-01 # administer
```

Feedback carries the numbers and the frame. It states what failed and by how
much, and never how to fix it.

## Requirements

A C++23 compiler. Verified on Apple clang 21 with libc++, and on GCC with
libstdc++ in continuous integration. Set `PROBE_CXX` to use a different one.
`./probe doctor` checks for `<print>` and ranges, because a model may
reasonably use them and a compiler that cannot would fail the submission
rather than the model.

Two standard libraries is not pedantry here. The first reference built
happily on a Mac for weeks and did not compile on Linux at all: it used the
initializer-list forms of `std::min` and `std::max` without including
`<algorithm>`, which libc++ pulls in behind another header and libstdc++
does not. Nothing in the mutation tests could have found that, because
every mutant shared the bug. PyYAML is needed to read the
task files, and Pillow is optional: without it the frame is still evaluated,
it just is not converted to PNG for the feedback message.

The predicate libraries have no dependencies at all, so `harness/mesh.py`
and `harness/image.py` run anywhere Python does.
