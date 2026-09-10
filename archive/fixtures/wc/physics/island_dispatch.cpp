// physics/island_dispatch.cpp
//
// Chooses how each simulation island is solved.
//
// There are two contact row implementations. The scalar rows solve one contact
// at a time and are what every island used before the wide pass landed. The
// wide rows gather four contacts into lanes and solve them together, which is
// a clear win on large islands and a clear loss on small ones, because the
// gather and scatter cost is paid whether or not the lanes are full.
//
// The crossover is a platform decision, not a global one: it depends on lane
// width and on how expensive an unaligned gather is on the target. It lives in
// the platform solver config rather than here.

#include "physics/island_dispatch.h"

#include "physics/contact_rows.h"
#include "physics/island.h"
#include "physics/solver_config.h"

namespace physics {
namespace {

// An island with a single body and no contacts still costs a dispatch. These
// are extremely common on levels with scattered debris, so they are handled
// before anything else looks at them.
bool IsTrivial(const Island& island) {
    return island.contact_count == 0 && island.body_count <= 1;
}

// Islands that contain a body flagged as player relevant are never split
// across jobs, so that the whole island is solved in one deterministic order.
bool MustStaySingleJob(const Island& island) {
    return island.contains_player_relevant;
}

}  // namespace

SolvePath ChooseSolvePath(const Island& island) {
    const SolverConfig& cfg = ActiveSolverConfig();

    if (IsTrivial(island)) {
        return SolvePath::kSkip;
    }
    if (island.body_count >= cfg.wide_island_threshold) {
        return SolvePath::kWideRows;
    }
    return SolvePath::kScalarRows;
}

int JobCountFor(const Island& island) {
    const SolverConfig& cfg = ActiveSolverConfig();
    if (MustStaySingleJob(island)) {
        return 1;
    }
    int jobs = island.contact_count / cfg.island_split_contacts;
    if (jobs < 1) {
        jobs = 1;
    }
    return jobs;
}

void DispatchIsland(Island& island, float dt_seconds) {
    const SolverConfig& cfg = ActiveSolverConfig();
    const SolvePath path = ChooseSolvePath(island);

    switch (path) {
        case SolvePath::kSkip:
            return;
        case SolvePath::kScalarRows:
            for (int i = 0; i < cfg.velocity_iterations; ++i) {
                SolveContactRowsScalar(island, dt_seconds);
            }
            break;
        case SolvePath::kWideRows:
            for (int i = 0; i < cfg.velocity_iterations; ++i) {
                SolveContactRowsWide(island, dt_seconds);
            }
            break;
    }

    for (int i = 0; i < cfg.position_iterations; ++i) {
        SolvePositionRows(island, dt_seconds);
    }
}

}  // namespace physics
