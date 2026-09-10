// sim/ballistics_fixed.cpp
//
// Ballistic arcs for thrown and lobbed projectiles, in fixed point.
//
// Every divide in this file goes through mathlib::FixedDiv rather than the
// language operator. That is not stylistic: the rounding decision belongs to
// the divide, it is declared next to the function in math/fixed_div.h, and it
// is part of the simulation contract. Writing a plain / here would silently
// pick the language's own rounding and diverge from every other divide in the
// simulation.

#include "sim/ballistics_fixed.h"

#include "math/fixed_div.h"
#include "math/sim_length.h"
#include "sim/entity_store.h"
#include "sim/sim_rates.h"

namespace sim {
namespace {

// Fixed point 16.16 helpers. Products are widened before the shift.
int32_t Mul16(int32_t a, int32_t b) {
    return static_cast<int32_t>((static_cast<int64_t>(a) * static_cast<int64_t>(b)) >> 16);
}

// Ticks of flight for a projectile launched with a given vertical velocity per
// tick against a given gravity per tick, ignoring drag. The divide is where
// the rounding mode shows up: the quotient is a tick count, so a difference of
// one raw unit here is a whole tick of flight on one peer and not on another.
int32_t FlightTicks(SimVelocity vertical_per_tick, SimVelocity gravity_per_tick) {
    const int32_t ratio = mathlib::FixedDiv(vertical_per_tick.raw(), gravity_per_tick.raw());
    return ratio * 2;
}

// Apex height above the launch point, in raw simulation length units.
int64_t ApexRaw(SimVelocity vertical_per_tick, SimVelocity gravity_per_tick) {
    const int32_t ticks_to_apex = mathlib::FixedDiv(vertical_per_tick.raw(), gravity_per_tick.raw());
    return static_cast<int64_t>(Mul16(ticks_to_apex, static_cast<int32_t>(vertical_per_tick.raw()))) / 2;
}

}  // namespace

// Solves the launch velocity that puts a projectile through a target point.
//
// The solver is iterative and bounded: it takes a fixed number of steps and
// returns the best candidate rather than iterating to a tolerance, because an
// iteration count that depends on the values is an iteration count that can
// differ between peers.
BallisticSolution SolveBallisticArc(const World& world,
                                    const SimVec3& launch,
                                    const SimVec3& target,
                                    SimVelocity speed_per_tick) {
    const SimVelocity gravity = world.tuning.gravity_per_tick;

    const int64_t dx = target.x.raw() - launch.x.raw();
    const int64_t dz = target.z.raw() - launch.z.raw();
    const int64_t dy = target.y.raw() - launch.y.raw();

    BallisticSolution best;
    best.valid = false;

    for (int step = 0; step < kBallisticSolverSteps; ++step) {
        const int32_t t = kBallisticFirstAngle + step * kBallisticAngleStep;
        const int32_t vertical = Mul16(static_cast<int32_t>(speed_per_tick.raw()), SinFixed(t));
        const int32_t horizontal = Mul16(static_cast<int32_t>(speed_per_tick.raw()), CosFixed(t));

        if (horizontal == 0) {
            continue;
        }

        const int32_t flight = FlightTicks(SimVelocity::FromRaw(vertical), gravity);
        const int64_t reach = static_cast<int64_t>(Mul16(horizontal, flight));
        const int64_t ground = IsqrtFixed(dx * dx + dz * dz);

        const int64_t error = reach - ground;
        const int64_t magnitude = error < 0 ? -error : error;
        if (!best.valid || magnitude < best.error_raw) {
            best.valid = true;
            best.error_raw = magnitude;
            best.angle = t;
            best.flight_ticks = flight;
            best.apex_raw = ApexRaw(SimVelocity::FromRaw(vertical), gravity) + dy;
        }
    }
    return best;
}

}  // namespace sim
