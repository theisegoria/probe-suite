// physics/solver_config.h
//
// Per platform solver budget.
//
// The solver has to fit inside a very different tick budget on each target, so
// the iteration counts, the island size at which the wide path is worth its
// setup cost, and the sleeping thresholds are all per platform. The struct is
// shared; exactly one translation unit per target defines the instance and is
// linked into that target's build:
//
//   solver_config_desktop.cpp
//   solver_config_console.cpp
//   solver_config_handheld.cpp
//
// The values are simulation affecting, so a match is only ever played between
// peers on targets whose configs agree. The lobby refuses to mix them.

#ifndef PHYSICS_SOLVER_CONFIG_H
#define PHYSICS_SOLVER_CONFIG_H

namespace physics {

struct SolverConfig {
    // Velocity and position iteration counts per tick.
    int velocity_iterations;
    int position_iterations;

    // Island body count at or above which the dispatcher uses the wide rows.
    int wide_island_threshold;

    // Contacts per island above which the island is split across jobs.
    int island_split_contacts;

    // Bodies below this linear and angular speed for the sleep delay are put
    // to sleep, in metres per second and radians per second.
    float sleep_linear_speed;
    float sleep_angular_speed;
    int sleep_delay_ticks;

    // Maximum contact rows the solver will allocate in a tick. Rows beyond
    // this are dropped, deepest penetration first, which is visible but is
    // better than blowing the frame.
    int max_contact_rows;
};

// The instance linked into this target.
const SolverConfig& ActiveSolverConfig();

}  // namespace physics

#endif  // PHYSICS_SOLVER_CONFIG_H
