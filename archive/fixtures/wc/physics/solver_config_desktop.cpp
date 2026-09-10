// physics/solver_config_desktop.cpp
//
// Solver budget for the desktop target.
//
// Desktop has the widest lanes and the cheapest gather of any target we ship,
// so the wide rows start paying off on quite small islands. It also has the
// largest tick budget, which is where the iteration counts come from.
//
// Linked only into the desktop build. See solver_config.h for the list of the
// other per target definitions.

#include "physics/solver_config.h"

namespace physics {
namespace {

constexpr SolverConfig kDesktopConfig = {
    /*velocity_iterations=*/10,
    /*position_iterations=*/4,

    // Islands of this many bodies or more take the wide path. Measured on the
    // warehouse stress level: below this the gather cost dominates, above it
    // the wide rows win by roughly a third.
    /*wide_island_threshold=*/32,

    /*island_split_contacts=*/512,

    /*sleep_linear_speed=*/0.04f,
    /*sleep_angular_speed=*/0.06f,
    /*sleep_delay_ticks=*/30,

    /*max_contact_rows=*/16384,
};

}  // namespace

const SolverConfig& ActiveSolverConfig() {
    return kDesktopConfig;
}

}  // namespace physics
