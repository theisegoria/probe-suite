# Knowledge-break probe

What does a model actually know about a specific technical world, where does
that knowledge give out, and what does the shape of the failure say about why.

Three domains, each chosen because the vendor publishes machine-readable,
versioned ground truth, and because the training text is lopsided in a way
that makes a wrong-era answer likely:

| domain | ground truth | the lopsidedness |
|---|---|---|
| Metal, MetalFX | Apple's documentation data feed | nine years of Metal 3, about one of Metal 4 |
| Vulkan 1.3 and 1.4 | the spec's versions appendix and `vk.xml` | extensions promoted to core still documented as extensions everywhere |
| PhysX vehicles 4.1 and 5.3 | per-release vehicle documentation | the 4.x vehicle API is in every tutorial; 5.1 replaced it wholesale |

Four instruments about reasoning rather than knowledge are parked in
`archive/`. They still validate and can be restored.

## How it works, and why publishing it does not destroy it

The suite ships a **generator and its templates, not an answer key**.

```
sources/     dated snapshots of vendor documentation
templates    question shapes, in suite/generate.py
generate.py  sources + shapes -> a fresh item set, seeded
run.py       administers it, by API or by pasting
score.py     scores mechanically against the source
report.py    prints the breakage map
```

Every item's answer is read out of the source snapshot at generation time.
There is no checked-in key to memorise, and two runs with different seeds ask
different questions. A model can of course memorise the vendor documentation,
and that is the point: memorising the vendor documentation *is* the knowledge
under test.

So yes, this can be published and other people can run it against their own
models. What they clone is the machinery. What they measure is drawn fresh.

**One caveat, stated plainly.** Refreshing a source snapshot is currently a
documented manual step, not an automated fetch. `python3 suite/sources.py`
prints every source URL and what it should say; `python3 suite/recheck.py`
prints the same for the fixed cases. Automating the fetch is the obvious next
step and was deliberately not shipped untested.

## The axes

One property varies at a time, so a drop localises its own cause.

| axis | what it varies | what a drop means |
|---|---|---|
| `recency` | age of the surface, 2017 to 2026 | a cliff is the training cutoff |
| `version-delta` | what changed between two versions | the superseded API dominates the training text |
| `coverage` | popular calls against obscure corners | knows the tutorial, not the domain |
| `depth` | four rungs on one surface it does know | surface memory: holds names, invents semantics |
| `fabrication` | symbols that do not exist | generating from the shape of the API, not recalling |
| `build` | a real task under a named version | whether it *uses* what it knows under pressure |

The Vulkan items are the sharpest version-delta material available anywhere:
39 extensions promoted to core across 1.3 and 1.4, each with an unarguable
answer, plus 19 new commands and 17 new structures in 1.4. The second Vulkan
template is the expensive one in practice: on a 1.4 device, do you still need
to enable `VK_KHR_push_descriptor`? The answer is no, and a model that says
yes has just written a silent portability bug.

## The build task

Recall questions ask whether a model holds an API. The build task asks
whether it uses it. Five vehicle setup tasks, each naming PhysX 5.3, graded
by harvesting every `Px…` symbol from the returned code and classifying it
against the real symbol index:

| classification | meaning |
|---|---|
| current | in 5.3 and not deprecated |
| deprecated | in 5.3 but marked for removal, such as `PxVehicleDrive4W` |
| wrong era | in 4.1 and not in 5.3, such as `PxVehicleUpdates` |
| unrecognised | in neither index |

`BUILD_WRONG_ERA` fires whether the answer named twenty old symbols or three,
because a single `PxVehicleWheelsSimData` in a 5.3 answer settles the
question. The report also prints **era purity**, the share of recognised
symbols that are current, and which previous-version symbols the model
reached for most often.

Unrecognised symbols are reported as unrecognised, not as fabricated. The
PhysX index covers the vehicle documentation for two releases, so absence
from it is weaker evidence than a 404 against Apple's symbol graph, and the
report says so where it matters.

## Running it

```
python3 suite/selftest.py                    # prove the scoring discriminates
python3 suite/generate.py --n 20 --seed 42   # draw an item set
python3 suite/selftest.py generated/<set>.json   # prove that set is scoreable
python3 suite/run.py --model paste --items generated/<set>.json
python3 suite/report.py results/<tag>.json --judge
```

`--seed` reproduces a set exactly, for comparing like with like. Omit it for
a fresh draw. The manual adapter needs no API access: it writes each prompt
to `runs/<tag>/`, you paste and save the replies beside them, and re-running
collects. With API access, fill in `config.yaml` and pass `--adapter` and
`--effort`.

The self-test synthesises the ideal and the failure response for every item
and asserts each lands in the right bucket. It also checks that no accepted
phrase is a substring of a forbidden one, which is not theoretical: it caught
exactly that in the fixed set, where accepting "commit" would have scored
"MTLCommandBuffer.commit" as correct and inverted the finding.

## Designed, not built

Two extensions were specified and deliberately left unbuilt rather than
shipped half-working.

**A rendering and behaviour loop.** The model writes a shader or a vehicle
setup, a harness compiles and runs it, and the model iterates against
structured failures and the rendered frame until it converges, with
iterations-to-convergence as the metric. The graphics half is
straightforward: a Mac has a real Metal compiler, and headless Chromium
gives a portable WebGL arm.

The vehicle half needs PhysX built for Apple Silicon, which upstream does
not ship. It is a bounded adaptor rather than a port, and the source is
closer to it than the preset list suggests:

- Platform detection is intact. `PxPreprocessor.h` sets `PX_OSX` from
  `__APPLE__`, and defines `PX_APPLE_FAMILY` and `PX_UNIX_FAMILY` so Apple
  is already inside the Unix family.
- The Unix paths already branch on it. `PxUnixFPU.h` guards on
  `PX_LINUX || PX_OSX`, not on Linux alone.
- NEON selection is architectural, not per-platform. `PX_NEON` comes from
  `__ARM_NEON`, which Apple clang defines on Apple Silicon, and the NEON
  headers sit under the Unix family.
- The build system still accepts a mac target.
  `GetCompilerAndPlatform.cmake` has a `TARGET_BUILD_PLATFORM STREQUAL
  "mac"` branch and `cmake_generate_projects.py` maps `mac64` onto it.
  22 files across the SDK still reference `PX_APPLE`, `PX_OSX` or
  `__APPLE__`.

What is missing is the build wiring: a `mac-aarch64-clang` preset, a
`source/compiler/cmake/mac/` directory mirroring the Linux one (15 files,
about 1100 lines, most of it near-identical under clang), and an arm64
branch where `GetCompilerAndPlatform.cmake` currently hardcodes
`mac.x86_${LIBPATH_SUFFIX}`. Plus whatever the first build turns up.

PyBullet or MuJoCo would give real vehicle dynamics for one pip install, at
the cost of no longer testing PhysX.

**Automated source refresh.** See the caveat above.

Neither is stubbed out anywhere in the code. Nothing here pretends to do
something it does not do.

## What this cannot tell you

**Absence is not equally provable across domains.** A 404 from Apple's feed
is proof a symbol does not exist. Absence from the Vulkan appendix or the
PhysX vehicle docs is not, so fabrication items are generated only for Metal.

**The NOSUCHTHING verdict tips the model off** that some items may be fake,
which puts the fabrication rate below what you would see in ordinary use. It
is a floor, not an estimate. Not offering it would be worse: a model that
correctly knows a symbol is fake would have no way to say so.

**Seeded draws are not interchangeable.** Two runs with different seeds ask
different questions, so compare distributions across many items, or fix the
seed. The report always states the denominator.

**The generator inherits its snapshot's date.** An item generated from a
stale snapshot is confidently wrong in exactly the way the instrument exists
to detect, which would be an embarrassing way to be wrong. Refresh before a
run that matters.

**Three domains, one shape of knowledge.** All three are versioned APIs with
published symbol graphs. Nothing here licenses a claim about the same model
on physics, medicine, or anything whose ground truth is not a symbol list.
