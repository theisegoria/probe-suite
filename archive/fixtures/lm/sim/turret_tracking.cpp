// engine/sim/turret_tracking.cpp
//
// Turret target selection and tracking.
//
// A turret picks a target, turns towards it, and fires when the barrel is
// pointed close enough. All three steps are simulation: the shot that comes
// out the end has to leave on the same tick, from the same facing, on every
// machine in the match.
//
// Facings are stored as turns (see math/fixed_trig.h). The tracking code
// currently steps the facing between the thirty two sector centres the old
// deployable turret used, which is why turrets look like they are ratcheting
// round rather than turning.

#include "sim/turret_tracking.h"

#include <algorithm>
#include <cstdint>

#include "core/types.h"
#include "math/fixed_trig.h"
#include "math/sim_math.h"
#include "sim/rng.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;
using math::kTurn;
using math::kHalfTurn;

// The legacy sector grid. Thirty two sectors, so one sector is 0x0800 turns,
// which is eleven and a quarter degrees.
constexpr uint32_t kSectorCount   = 32;
constexpr Q16 kSectorSize         = kTurn / kSectorCount;
constexpr Q16 kSectorHalf         = kSectorSize / 2;

constexpr Q16 kMaxRangeQ16        = core::FromInt(120);
constexpr uint32_t kReacquireTicks = 12;
constexpr Q16 kTargetLeadScale    = core::kQ16One / 2;

Q16 SectorCentreTurns(uint32_t sector) {
    return static_cast<Q16>((sector * kSectorSize) & 0xFFFF);
}

uint32_t SectorOfTurns(Q16 turns) {
    const uint32_t wrapped = static_cast<uint32_t>(turns) & 0xFFFFu;
    return ((wrapped + kSectorHalf) / kSectorSize) % kSectorCount;
}

// Squared planar distance in Q16 metres. Turrets ignore height: the mounts
// only traverse, they do not elevate.
Q16 PlanarDistanceSqQ16(const Transform& a, const Transform& b) {
    const Q16 dx = a.position_q16.x - b.position_q16.x;
    const Q16 dy = a.position_q16.y - b.position_q16.y;
    return core::MulQ16(dx, dx) + core::MulQ16(dy, dy);
}

}  // namespace

// ------------------------------------------------------------ muzzle work

// Where the barrel tip sits, given the mount position and the current facing.
// The mount offset is authored in the turret's own frame and rotated into the
// world by the current facing.
Vec2Q16 MuzzlePosition(const TurretState& turret, const Transform& mount) {
    const Q16 s = math::SinTurns(turret.facing_turns);
    const Q16 c = math::CosTurns(turret.facing_turns);

    const Q16 local_x = turret.def->muzzle_offset_x;
    const Q16 local_y = turret.def->muzzle_offset_y;

    Vec2Q16 out;
    out.x = mount.position_q16.x + core::MulQ16(local_x, c) - core::MulQ16(local_y, s);
    out.y = mount.position_q16.y + core::MulQ16(local_x, s) + core::MulQ16(local_y, c);
    return out;
}

// Unit vector down the barrel, in Q16.
Vec2Q16 BarrelDirection(const TurretState& turret) {
    Vec2Q16 out;
    out.x = math::CosTurns(turret.facing_turns);
    out.y = math::SinTurns(turret.facing_turns);
    return out;
}

// ---------------------------------------------------------- target choice

// Candidate targets are gathered from the sim broadphase in slot order and
// scored. Ties are broken by entity id so that two equally good targets
// always resolve the same way.
EntityId TurretSystem::ChooseTarget(World& world, const TurretState& turret, const Transform& mount) const {
    EntityId best = core::kInvalidEntity;
    Q16 best_score = 0;

    const Q16 range_sq = core::MulQ16(turret.def->range_q16, turret.def->range_q16);

    for (core::Slot slot = 0; slot < world.actor_count(); ++slot) {
        const Actor& actor = world.actor(slot);
        if (!actor.alive) continue;
        if (actor.team == turret.team) continue;
        if (actor.cloaked_ticks > 0) continue;

        const Q16 dist_sq = PlanarDistanceSqQ16(mount, actor.transform);
        if (dist_sq > range_sq) continue;

        Q16 score = core::DivQ16(core::kQ16One, dist_sq + core::kQ16One);
        if (actor.entity == turret.last_target) {
            // Sticky targeting, so a turret does not flip between two equally
            // close targets every tick.
            score += turret.def->target_stickiness;
        }
        score += world.threat().ThreatOf(actor.entity);

        if (score > best_score || (score == best_score && actor.entity < best)) {
            best_score = score;
            best = actor.entity;
        }
    }

    return best;
}

// ------------------------------------------------------------- tracking

// Step the turret's facing towards its target.
//
// The mount can only sit on one of the thirty two sector centres, so this
// works out which sector the target is in and moves one sector per tick until
// it gets there.
void TurretSystem::TrackTarget(World& world, TurretState& turret, const Transform& mount) {
    if (turret.target == core::kInvalidEntity) {
        turret.tracking = false;
        return;
    }

    const Actor& target = world.actor_by_entity(turret.target);

    const Q16 dx = target.transform.position_q16.x - mount.position_q16.x;
    const Q16 dy = target.transform.position_q16.y - mount.position_q16.y;

    const Q16 desired_turns = math::AtanTurns(dy, dx);
    const uint32_t desired_sector = SectorOfTurns(desired_turns);
    const uint32_t current_sector = SectorOfTurns(turret.facing_turns);

    if (desired_sector == current_sector) {
        turret.facing_turns = SectorCentreTurns(current_sector);
        turret.tracking = false;
        return;
    }

    // Shortest way round the sector ring.
    const int32_t raw = static_cast<int32_t>(desired_sector) - static_cast<int32_t>(current_sector);
    int32_t step = raw;
    if (step > static_cast<int32_t>(kSectorCount / 2)) step -= static_cast<int32_t>(kSectorCount);
    if (step < -static_cast<int32_t>(kSectorCount / 2)) step += static_cast<int32_t>(kSectorCount);

    const uint32_t next_sector = (current_sector + (step > 0 ? 1u : kSectorCount - 1u)) % kSectorCount;

    turret.facing_turns = SectorCentreTurns(next_sector);
    turret.tracking = true;
}

// ---------------------------------------------------------------- firing

// A turret fires when it is on target and its cooldown has run out. On target
// currently means the sector matches, which is why turrets can put a burst
// eleven degrees wide of a moving target.
bool TurretSystem::CanFire(const World& world, const TurretState& turret, const Transform& mount) const {
    if (turret.cooldown_ticks > 0) return false;
    if (turret.target == core::kInvalidEntity) return false;
    if (turret.ammo == 0) return false;

    const Actor& target = world.actor_by_entity(turret.target);
    const Q16 dx = target.transform.position_q16.x - mount.position_q16.x;
    const Q16 dy = target.transform.position_q16.y - mount.position_q16.y;

    const Q16 desired_turns = math::AtanTurns(dy, dx);
    return SectorOfTurns(desired_turns) == SectorOfTurns(turret.facing_turns);
}

void TurretSystem::Fire(World& world, TurretState& turret, const Transform& mount) {
    Rng rng = Derive(world.match_seed, RngStream::kCombat, world.tick, turret.entity);

    const Q16 spread = rng.RangeQ16(-turret.def->spread_turns, turret.def->spread_turns);
    const Q16 shot_turns = static_cast<Q16>((turret.facing_turns + spread) & 0xFFFF);

    ProjectileSpawn spawn;
    spawn.origin = MuzzlePosition(turret, mount);
    spawn.direction_turns = shot_turns;
    spawn.speed_q16 = turret.def->muzzle_speed_q16;
    spawn.owner = turret.entity;
    spawn.damage = turret.def->damage;

    world.QueueProjectile(spawn);

    turret.cooldown_ticks = turret.def->cooldown_ticks;
    turret.ammo -= 1;
    turret.shots_fired += 1;
}

// ------------------------------------------------------------------ tick

void TurretSystem::Tick(World& world) {
    for (core::Slot slot = 0; slot < world.turret_count(); ++slot) {
        TurretState& turret = world.turret(slot);
        if (!turret.powered) continue;

        const Transform& mount = world.transform_of(turret.entity);

        if (turret.cooldown_ticks > 0) turret.cooldown_ticks -= 1;

        if (turret.reacquire_ticks > 0) {
            turret.reacquire_ticks -= 1;
        } else {
            const EntityId chosen = ChooseTarget(world, turret, mount);
            if (chosen != turret.target) {
                turret.last_target = turret.target;
                turret.target = chosen;
                turret.reacquire_ticks = kReacquireTicks;
            }
        }

        TrackTarget(world, turret, mount);

        if (CanFire(world, turret, mount)) {
            Fire(world, turret, mount);
        }
    }
}

}  // namespace sim
