# probe suite

[![self test](https://github.com/theisegoria/probe-suite/actions/workflows/selftest.yml/badge.svg)](https://github.com/theisegoria/probe-suite/actions/workflows/selftest.yml)

Three benchmarks you can download and run against your own models. They test
things the public leaderboards do not report: what a model actually knows
about a versioned API and where that knowledge gives out, whether it can
write geometry that is correct rather than merely plausible, and whether it
can make a vehicle work under real physics and drive it somewhere.

```
git clone https://github.com/theisegoria/probe-suite
cd probe-suite
./probe doctor
```

`doctor` tells you what is installed, what each benchmark needs, and which
of the three you can run right now. `./probe selftest` then proves the
scoring works before you spend anything on a run.

## The three

**Knowledge.** What does a model hold about Metal, Vulkan 1.3 against 1.4,
and the PhysX vehicle API, where does it give out, and what does the shape
of the failure say about why. Recency, version delta, coverage, depth, and
symbols that do not exist. Ships a generator rather than an answer key, so
publishing it does not destroy it: items are drawn fresh per seeded run and
every answer is read out of a dated snapshot of vendor documentation.
Needs only Python. [Details](docs/knowledge.md).

```
./probe knowledge selftest
./probe knowledge generate --n 20
./probe knowledge run --model paste --items generated/<set>.json
```

**Procedural geometry.** The model writes one self-contained C++23 file that
generates a mesh, writes it as an OBJ, and rasterises it with a renderer it
also wrote. Standard library only: no mesh package, no image package, no
GPU. Then it iterates against structured failures and the frame it produced.
Correctness is arithmetic rather than opinion, because the geometry is
checked as well as the pixels: a torus either has Euler characteristic zero
or it does not. Needs a C++23 compiler. [Details](docs/mesh.md).

```
./probe mesh selftest
./probe mesh run --model paste --task mr-01
```

**Jeep test drive.** The suite ships a terrain and a vehicle mesh, and the
model has to make the vehicle work under PhysX 5 and drive it through five
waypoints over a ramp and a cross slope. One self-contained C++ file, built
against the real SDK. This is the one that tests whether a model can work
inside somebody else's large versioned API rather than recite it, and the
ladder separates a vehicle that will not sit still from one that will not
steer from one that rolls on the slope. Needs a built PhysX 5;
`adaptors/physx-apple-silicon/` builds it on Apple Silicon, which upstream
does not, and Linux and Windows use upstream presets.
[Details](docs/jeep.md).

```
./probe jeep selftest
./probe jeep run --model paste --task jd-01
```

## Results are ladders, not pass rates

A pass rate collapses "did not compile" and "built a beautiful torus with
inverted normals" into the same number. Every benchmark here reports the
rung a model reached, because a model that stops at "closed 2-manifold" and
never reaches "topology matches the spec" is making a different mistake from
one that never compiles, and you want to know which.

## Every benchmark proves its own scoring first

This is the part worth copying even if you take nothing else. A scoring
harness that reports "passed" for everything looks identical to a working
one in a summary table, so each benchmark ships a self-test that breaks a
known-good solution in one specific way per rung and asserts each mutant
stops exactly where it should.

Mutants alone are only half of it, because a ladder can fail in two
opposite directions. One is answering 7 to everything. The other is being
tuned so tightly around one solution that it fails every model for reasons
that are the benchmark's fault, and no amount of breaking that solution
will ever reveal it. So each ladder also has a second reference, written to
share as little as possible with the first, that has to reach the top:

```
mr-01b   a swept circle on an explicit frame instead of the closed form,
         40 by 29 instead of 48 by 24, v major instead of u major, the
         other quad diagonal, and a painter's algorithm renderer with no
         depth buffer
jd-01b   the wheels numbered the other way round, front is 2 and 3, with
         pure pursuit against a lookahead point instead of heading error,
         and a PI speed controller instead of a proportional one
```

Both reached rung 7 on their first run, which is the result worth having:
the ladders were not measuring the references.

```
./probe jeep selftest
  ok    unmodified reference               rung 7 (expected 7)
  ok    does not compile                   rung 0 (expected 0)
  ok    compiles but exits non zero        rung 1 (expected 1)
  ok    writes no trajectory               rung 2 (expected 2)
  ok    spawned too high, still falling    rung 3 (expected 3)
  ok    never applies throttle             rung 4 (expected 4)
  ok    steers the wrong way               rung 5 (expected 5)
  ok    gets round, but far too slowly     rung 6 (expected 6)
```

```
./probe mesh selftest
  ok    unmodified reference               rung 7 (expected 7)
  ok    does not compile                   rung 0 (expected 0)
  ok    compiles but exits non zero        rung 1 (expected 1)
  ok    writes no geometry                 rung 2 (expected 2)
  ok    open surface, not closed           rung 3 (expected 3)
  ok    closed but normals inward          rung 4 (expected 4)
  ok    geometry right, renders nothing    rung 5 (expected 5)
  ok    renders a flat unlit silhouette    rung 6 (expected 6)
```

One command runs all of it:

```
./probe selftest
```

It picks its steps from what the machine can do and says what it skipped,
because a suite that quietly ran three of five checks and reported success
would be worse than one that failed. `--require` turns a skip into a
failure, which is what the CI workflow uses.

It earns its keep. The mesh reference solution first stopped at rung 6 on a
symmetry check, and the renderer was right: the predicate measured luminance
symmetry, which an off-axis light legitimately breaks. The check now
measures the silhouette against its own mirror. The jeep reference then
found two more: a terrain whose triangles were wound so their normals
pointed downwards, invisible to the raycast the suspension uses, and a
finish that was never written to the trace because the program stopped one
tick too early. Writing the tests for the harness itself then found a
fourth, where an explicit `PROBE_PHYSX_ROOT` pointing somewhere wrong fell
back to the bundled checkout and built against a different SDK than the
operator asked for, silently. A reference solution is there to catch
unsatisfiable tasks, and between them they caught four.

## Ground truth has a shelf life

The knowledge benchmark reads its answers out of dated snapshots of vendor
documentation. A stale snapshot does not make it noisier, it makes it wrong
in one direction: API the vendor shipped after the snapshot is scored as
fabrication, and a model is marked down for knowing more than the harness.

So the snapshots have a clock on them. Past 90 days the self-test says so,
past 180 it fails, and `PROBE_ALLOW_STALE_SOURCES=1` downgrades that to a
warning for anyone who has checked by hand that the vendor has not moved.
`python3 suite/sources.py` prints what to re-read.

## Running against a model

Every benchmark defaults to a manual adapter that needs no API key at all:
it writes each prompt to `runs/<tag>/`, you paste it into whatever you are
testing and save the reply beside it, then re-run to collect and score. With
API access, copy `config.example.yaml` to `config.yaml`, fill in the
parameter shapes you have actually observed for your models, and pass
`--adapter` and `--effort`.

## What is here

```
probe                     one entry point for everything
suite/                    knowledge benchmark, and the shared runner
harness/                  geometry and jeep benchmarks: predicates, ladders, references
assets/                   the terrain, the jeep, and where each came from
fixtures/                 recorded trajectories, so the jeep ladder is testable without PhysX
sources/                  dated snapshots of vendor documentation
cases/                    task and item definitions
adaptors/                 PhysX 5 for Apple Silicon
archive/                  four instruments about reasoning rather than knowledge
docs/                     the long form for each benchmark
.github/                  the self test, on every push
```

## Requirements

Python 3.8 or later for everything. PyYAML to read the task files. A C++23
compiler for the geometry benchmark, verified on Apple clang 21. Pillow is
optional and only converts frames for feedback. A built PhysX 5 for the jeep
benchmark.

MIT. PhysX itself is NVIDIA's, under its own licence, and nothing from its
source tree is copied here.
