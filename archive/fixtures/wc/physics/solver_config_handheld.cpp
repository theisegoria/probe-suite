// physics/solver_config_handheld.cpp
//
// Solver budget for the handheld target.
//
// The handheld part has narrow lanes and an expensive unaligned gather, so the
// wide rows only start winning on much larger islands than on desktop. The
// profiling pass that set this number found the crossover moved by a factor of
// four, and rounded up rather than down because the wide path also costs more
// cache pressure, which the handheld feels first.
//
// The iteration counts are lower for the obvious reason: the tick budget is
// roughly a third of the desktop one at the same tick rate.
//
// Linked only into the handheld build.

#include "physics/solver_config.h"

namespace physics {
namespace {

constexpr SolverConfig kHandheldConfig = {
    /*velocity_iterations=*/6,
    /*position_iterations=*/2,

    // Islands of this many bodies or more take the wide path.
    /*wide_island_threshold=*/128,

    /*island_split_contacts=*/256,

    /*sleep_linear_speed=*/0.04f,
    /*sleep_angular_speed=*/0.06f,
    /*sleep_delay_ticks=*/24,

    /*max_contact_rows=*/6144,
};

}  // namespace

const SolverConfig& ActiveSolverConfig() {
    return kHandheldConfig;
}

}  // namespace physics
