// physics/island_solver.cpp
//
// Sequential impulse solver over one island.
//
// The island arrives with its contacts already built and sorted by contact id,
// which is what makes the solve deterministic: the impulses depend on the
// order the rows are visited, so the order has to be a function of simulation
// state and nothing else. Never sort by pointer, never sort by broadphase
// order, never parallelise a single island's rows.
//
// The per row maths lives in contact_rows.cpp, behind contact_rows.h. This
// file owns iteration counts, warm starting and the normal before friction
// ordering; it does not own any of the row tunables.

#include "physics/island_solver.h"

#include "physics/contact_rows.h"
#include "physics/contact_rows_v1.h"
#include "physics/island.h"
#include "physics/solver_config.h"
#include "sim/sim_rates.h"

namespace physics {
namespace {

// Warm starting. Last tick's accumulated impulse is applied before the first
// iteration, which is what lets a stack of crates converge in single digit
// iteration counts instead of visibly sinking every tick.
void ApplyWarmStart(Island& island) {
    for (int i = 0; i < island.row_count; ++i) {
        ContactRow& row = island.rows[i];
        if (!row.was_persistent) {
            row.normal_impulse = 0.0f;
            row.friction_impulse = 0.0f;
            continue;
        }
        ApplyRowImpulse(island, row, row.normal_impulse, row.friction_impulse);
    }
}

// Friction is solved after the normal impulse of the same iteration, using the
// normal impulse just accumulated for the friction bound. Solving it before
// makes boxes slide on the first frame of contact.
void SolveVelocityIteration(Island& island, float dt_seconds) {
    for (int i = 0; i < island.row_count; ++i) {
        SolveContactRow(island, island.rows[i], dt_seconds);
    }
    for (int i = 0; i < island.row_count; ++i) {
        SolveFrictionRow(island, island.rows[i]);
    }
}

// Bodies that have been slow for long enough stop being integrated at all.
// Sleeping is per island: one moving body keeps the whole island awake, which
// avoids the classic artefact of half a stack freezing mid collapse.
bool IslandCanSleep(const Island& island, const SolverConfig& cfg) {
    if (island.ticks_below_sleep_speed < cfg.sleep_delay_ticks) {
        return false;
    }
    for (int i = 0; i < island.body_count; ++i) {
        const Body& body = island.bodies[i];
        if (body.linear_speed > cfg.sleep_linear_speed) {
            return false;
        }
        if (body.angular_speed > cfg.sleep_angular_speed) {
            return false;
        }
    }
    return true;
}

}  // namespace

void SolveIsland(Island& island, float dt_seconds) {
    const SolverConfig& cfg = ActiveSolverConfig();

    PrepareContactRows(island, dt_seconds);
    ApplyWarmStart(island);

    for (int iteration = 0; iteration < cfg.velocity_iterations; ++iteration) {
        SolveVelocityIteration(island, dt_seconds);
    }

    IntegrateVelocities(island, dt_seconds);

    for (int iteration = 0; iteration < cfg.position_iterations; ++iteration) {
        SolvePositionRows(island, dt_seconds);
    }

    if (IslandCanSleep(island, cfg)) {
        island.sleeping = true;
    } else {
        island.ticks_below_sleep_speed = IslandIsSlow(island, cfg)
                                             ? island.ticks_below_sleep_speed + 1
                                             : 0;
    }
}

// The replay validator re runs recorded matches and compares the resulting
// state hash tick by tick. Recordings made before 2.7 are solved with the
// frozen rows in contact_rows_v1.cpp instead, because the row maths changed.
void SolveIslandForLegacyReplay(Island& island, float dt_seconds) {
    const SolverConfig& cfg = ActiveSolverConfig();

    rows_v1::PrepareContactRows(island, dt_seconds);
    ApplyWarmStart(island);
    for (int iteration = 0; iteration < cfg.velocity_iterations; ++iteration) {
        for (int i = 0; i < island.row_count; ++i) {
            rows_v1::SolveContactRow(island, island.rows[i], dt_seconds);
        }
        for (int i = 0; i < island.row_count; ++i) {
            rows_v1::SolveFrictionRow(island, island.rows[i]);
        }
    }
    IntegrateVelocities(island, dt_seconds);
}

}  // namespace physics
