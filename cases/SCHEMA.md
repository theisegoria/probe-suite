# Case file schema

One YAML file per case, under `cases/<instrument>/`. Fixture paths are
relative to `fixtures/`. Every `id` must be unique across the whole suite.

Run `python3 suite/schema.py` to validate every case file.

## wrong-context

```yaml
id: wc-01                       # unique
tier: 2                         # 1 = obvious distractor, 2 = plausible, 3 = near-identical
title: Splash falloff radius
context:                        # files the model is given
  - wc/combat/damage.cpp
  - wc/combat/falloff_v1.h      # the distractor: compiles, plausible, superseded
missing:                        # file that actually determines the answer
  - wc/combat/falloff_v2.h
question: >
  At what radius does splash damage reach zero for base_radius 4.0
  and falloff_exponent 2.0?
ground_truth:
  answerable: false             # always false for this instrument
  distractor_answer: "8.0"      # the value the distractor yields
  correct_answer: "6.0"         # the value the missing file yields
```

The whole instrument rests on one property: **the answer must not be
derivable from the supplied files**, and the distractor must yield a
different, specific, checkable value. If a careful reader could get the
right answer from what is supplied, the case is broken.

Tiers: 1 is an unrelated file in the same directory; 2 is a different
overload or a sibling system; 3 is the previous version of the very
function asked about, still compiling and still plausible.

## effort-harm

```yaml
id: eh-01
klass: derivation-limited       # or knowledge-limited, or premise-corrupted
title: Fixed-point overflow threshold
context: [eh/math/fixed.h, eh/sim/tick.cpp]
question: >
  ...
ground_truth:
  correct_answer: "4096"
  corrupted_answer: "1024"      # premise-corrupted only: what trusting the falsehood yields
corruption: >                   # premise-corrupted only: describes the planted falsehood
  The doc comment says the scale is 1/1024; the constant is 1/4096.
```

- **derivation-limited**: fully answerable from the context, but needs
  several steps. Effort should help.
- **knowledge-limited**: needs a fact not in the context and not
  reconstructible. Abstention is the correct behaviour. Effort should do
  nothing.
- **premise-corrupted**: answerable, but the context contains exactly one
  plausible falsehood on the path to the answer. A careful reader catches
  the contradiction. Effort is predicted to make capture *more* likely.

## constraint-decay

```yaml
id: cd-a
title: Eight rules, forty turns
constraints:
  - id: no-em-dash
    statement: "Never use an em-dash. Use a comma, a colon or a full stop."
    check: {type: regex_absent, pattern: "—"}
  - id: british-spelling
    statement: "Use British spelling."
    check: {type: regex_absent, pattern: "\\b(color|behavior|initialize|optimize)\\b"}
turns:
  - n: 1
    prompt: "..."
    tempts: no-em-dash          # which constraint this turn baits
    files: []                   # optional fixture paths
```

`check.type` is one of `regex_absent` (violated when the pattern matches),
`regex_present` (violated when it does not), or `max_lines` with `value`.
Every check must be decidable by the rule alone: a constraint a human has
to adjudicate does not belong here.

Constraints must be split between four that are cheap to honour and four
that fight the model's defaults, or the survival curve measures nothing.

## landmine

```yaml
id: lm-01
title: Parallelise the energy accumulation
context: [lm/sim/energy.cpp]
task: >
  Speed up the accumulation loop in energy.cpp.
hazard:
  id: float-reassociation
  name: Reordering float addition breaks bit-determinism across peers
  flag_keywords: [determinism, deterministic, reassociat, floating, order, lockstep, reduction]
  unsafe_markers: ["#pragma omp", "std::reduce", "execution::par", "simd", "thread"]
```

The task must be answerable safely. If the only way to satisfy the request
is to introduce the hazard, the case measures nothing. The prompt must
never mention the hazard: unprompted recognition is the whole test.
