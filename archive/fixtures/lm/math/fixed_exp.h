// engine/math/fixed_exp.h
//
// Decay and growth helpers for the simulation.
//
// The simulation runs at a fixed rate, so anything that decays over time
// decays by a constant factor per tick. This header turns a half life, in
// ticks, into that per tick factor using a table baked by
// tools/gen_decay_table.py and checked in as math/decay_table.inc.
//
// The table holds 2^(-1/n) for n in [1, 1024] as Q16 factors, plus a second
// table of the same values in Q32 for the accumulators that need the extra
// bits over long durations.

#pragma once

#include <cstdint>

#include "core/types.h"

namespace math {

using core::Q16;
using core::Q32;
using core::kQ16One;

inline constexpr uint32_t kMaxHalfLifeTicks = 1024;

// Per tick multiplier for a value with the given half life. A half life of
// n ticks returns 2^(-1/n) in Q16. Half lives above kMaxHalfLifeTicks are
// clamped, half lives below one tick return zero.
Q16 HalfLifeFactorQ16(uint32_t half_life_ticks);

// Same in Q32, for stacks and heat values that are integrated over minutes.
Q32 HalfLifeFactorQ32(uint32_t half_life_ticks);

// Fraction remaining after elapsed_ticks of a half life. Equivalent to
// applying HalfLifeFactorQ16 elapsed_ticks times, but computed in one step
// from the same table, so the two agree bit for bit.
Q16 RemainingAfterQ16(uint32_t half_life_ticks, uint32_t elapsed_ticks);

// The inverse: how many ticks until only fraction_q16 of the value is left.
uint32_t TicksUntilRemainingQ16(uint32_t half_life_ticks, Q16 fraction_q16);

// Time constant form, for designers who author a rate rather than a half
// life. tau_ticks is the number of ticks to fall to 1/e of the starting
// value, and this returns the per tick factor for that curve.
Q16 TimeConstantFactorQ16(uint32_t tau_ticks);

}  // namespace math
