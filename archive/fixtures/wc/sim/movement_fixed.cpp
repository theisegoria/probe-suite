// sim/movement_fixed.cpp
//
// Character and vehicle movement integration, in fixed point.
//
// Everything here is simulation state. The rules are the usual ones: no float
// anywhere, no division except through the fixed point helpers, and no
// iteration over a container whose order is not itself simulation state.
//
// Velocities are stored as length per tick rather than per second, so
// integration is a plain add with no scaling step at all. Every quantity below
// is a SimLength or a SimVelocity, and nothing here touches the raw storage
// except through FromRaw, which is used only for the sign flips.

#include "sim/movement_fixed.h"

#include "math/sim_length.h"
#include "sim/entity_store.h"
#include "sim/sim_rates.h"
#include "sim/terrain_query.h"

namespace sim {
namespace {

// Gravity, as a velocity delta per tick. Loaded from the tuning table at level
// load and stored in simulation state so that a mid match tuning reload is
// itself a simulation event rather than a silent divergence.
SimVelocity GravityPerTick(const World& world) {
    return world.tuning.gravity_per_tick;
}

// Terminal velocity, likewise from the tuning table.
SimVelocity TerminalVelocity(const World& world) {
    return world.tuning.terminal_velocity_per_tick;
}

SimLength AbsLength(SimLength v) {
    return v.raw() < 0 ? SimLength::FromRaw(-v.raw()) : v;
}

// Ground snapping. A character walking down a gentle slope would otherwise
// leave the ground every tick and be caught by gravity on the next one, which
// reads as a rattle on the camera.
bool ShouldSnapToGround(const World& world, const MoveState& state, SimLength drop) {
    if (!state.was_grounded) {
        return false;
    }
    return drop < world.tuning.ground_snap_distance;
}

}  // namespace

// Integrates one entity's velocity and position for one tick.
void IntegrateMovement(World& world, EntityId id, MoveState& state) {
    if (!state.grounded) {
        state.velocity.y = state.velocity.y - GravityPerTick(world);
        const SimVelocity terminal = TerminalVelocity(world);
        if (AbsLength(state.velocity.y) > terminal) {
            state.velocity.y = state.velocity.y.raw() < 0
                                   ? SimLength::FromRaw(-terminal.raw())
                                   : terminal;
        }
    }

    const SimVec3 previous = state.position;

    state.position.x = state.position.x + state.velocity.x;
    state.position.y = state.position.y + state.velocity.y;
    state.position.z = state.position.z + state.velocity.z;

    const SimLength ground_height = TerrainHeightAt(world, state.position.x, state.position.z);
    const SimLength drop = ground_height - state.position.y;

    state.was_grounded = state.grounded;
    state.grounded = false;

    if (state.position.y < ground_height) {
        state.position.y = ground_height;
        state.velocity.y = SimLength::FromRaw(0);
        state.grounded = true;
    } else if (ShouldSnapToGround(world, state, drop)) {
        state.position.y = ground_height;
        state.velocity.y = SimLength::FromRaw(0);
        state.grounded = true;
    }

    // Distance travelled this tick, used by the footstep and the anti cheat
    // speed check. Computed from the fixed point positions, never from the
    // presentation transform.
    state.last_step = SimVec3{state.position.x - previous.x,
                              state.position.y - previous.y,
                              state.position.z - previous.z};

    world.store.WriteMoveState(id, state);
}

// Applies an external impulse, already expressed as a velocity delta per tick
// by whoever produced it, and clamps the result to the entity's speed cap.
void ApplyMovementImpulse(World& world, MoveState& state, const SimVec3& delta_per_tick) {
    state.velocity.x = state.velocity.x + delta_per_tick.x;
    state.velocity.y = state.velocity.y + delta_per_tick.y;
    state.velocity.z = state.velocity.z + delta_per_tick.z;

    const SimVelocity cap = world.tuning.max_speed_per_tick;
    if (AbsLength(state.velocity.x) > cap) {
        state.velocity.x = state.velocity.x.raw() < 0 ? SimLength::FromRaw(-cap.raw()) : cap;
    }
    if (AbsLength(state.velocity.z) > cap) {
        state.velocity.z = state.velocity.z.raw() < 0 ? SimLength::FromRaw(-cap.raw()) : cap;
    }
}

}  // namespace sim
