# probe suite

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
of the three you can run right now.

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

**Jeep test drive.** Not built yet. The suite will ship a terrain and a
vehicle mesh, and the model has to make the vehicle work under PhysX 5 and
drive it round an obstacle course. The physics substrate exists:
`adaptors/physx-apple-silicon/` builds PhysX 5 on Apple Silicon, which
upstream does not ship, and Linux and Windows use upstream presets.

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

It earns its keep. The mesh reference solution first stopped at rung 6 on a
symmetry check, and the renderer was right: the predicate measured luminance
symmetry, which an off-axis light legitimately breaks. The check now
measures the silhouette against its own mirror. A reference solution is
there to catch unsatisfiable tasks, and it caught one.

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
harness/                  geometry benchmark: predicates, ladder, reference
sources/                  dated snapshots of vendor documentation
cases/                    task and item definitions
adaptors/                 PhysX 5 for Apple Silicon
archive/                  four instruments about reasoning rather than knowledge
docs/                     the long form for each benchmark
```

## Requirements

Python 3.8 or later for everything. PyYAML to read the task files. A C++23
compiler for the geometry benchmark, verified on Apple clang 21. Pillow is
optional and only converts frames for feedback. PhysX 5 for the jeep
benchmark when it lands.

MIT. PhysX itself is NVIDIA's, under its own licence, and nothing from its
source tree is copied here.
