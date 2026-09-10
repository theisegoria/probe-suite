// math/sim_length.h
//
// The runtime simulation length scale.
//
// Finer than the cooked asset scale, for two reasons. Velocities are stored as
// lengths per tick, and at sixty ticks a second a slow moving body must still
// accumulate a non zero raw delta every tick or it will simply never move.
// Second, the solver's penetration slop is five millimetres, and a slop needs
// several representable steps inside it or contacts chatter between the last
// representable value inside the slop and the first one outside it.
//
// One raw unit is an eighth of a millimetre, that is 0.125 mm.

#ifndef MATH_SIM_LENGTH_H
#define MATH_SIM_LENGTH_H

#include "math/fixed_point.h"

namespace sim {

// Raw units per metre in the simulation.
constexpr int kSimUnitsPerMetre = 8000;

// Sixty four bit storage: at this scale a thirty two bit raw would run out of
// range around 268 kilometres, which sounds like plenty until a projectile
// with a bad velocity is integrated for a few thousand ticks and wraps.
using SimLength = mathlib::FixedLength<int64_t, kSimUnitsPerMetre>;

struct SimVec3 {
    SimLength x;
    SimLength y;
    SimLength z;
};

// Velocity is stored as length per tick rather than per second, so that
// integration is an addition with no scaling step at all.
using SimVelocity = SimLength;

}  // namespace sim

#endif  // MATH_SIM_LENGTH_H
