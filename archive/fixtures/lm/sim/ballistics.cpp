// engine/sim/ballistics.cpp
//
// Projectile flight.
//
// Every bullet, shell, rocket and thrown object in the match is a row in the
// projectile array. Once a tick every live row is advanced by one timestep,
// tested against the collision world, and either kept or retired.
//
// This is the hottest simulation file in the profile. A late game siege has
// around two thousand projectiles in flight and IntegrateProjectiles is
// roughly six percent of the tick.
//
// Everything below is written in the sim math house style: products that feed
// an addition go through MulThenAdd or AccumulateProduct so that the product
// rounds before the sum, and the translation unit is built with the sim math
// flag set (see build/sim_math.cmake).

#include "sim/ballistics.h"

#include <algorithm>
#include <cstdint>

#include "core/cpu_features.h"
#include "core/types.h"
#include "math/sim_math.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;

constexpr float kGravity           = -9.80665f;
constexpr float kAirDensitySea     = 1.225f;
constexpr float kMinSpeedSq        = 1.0e-4f;
constexpr uint32_t kMaxProjectiles = 4096;
constexpr uint32_t kMaxSubsteps    = 4;

// How many rows we like to touch between prefetches. Sized off the cache line
// so that a batch is a whole number of lines of the position array.
uint32_t RowsPerBatch() {
    const uint32_t line = core::g_cpu.cache_line_bytes;
    const uint32_t rows = line / static_cast<uint32_t>(sizeof(float) * 3);
    return rows == 0 ? 4u : rows;
}

}  // namespace

// ------------------------------------------------------------ type table

// Ballistic coefficients are authored per projectile type in
// data/weapons/ballistics.json. The table is small, a couple of hundred
// entries, and is looked up by type id.
const BallisticProfile& ProjectileSystem::ProfileFor(uint16_t type_id) const {
    if (type_id >= profile_count_) {
        return profiles_[0];   // the inert fallback profile
    }
    return profiles_[type_id];
}

// The drag term for one step. Quadratic drag, so the deceleration goes with
// the square of the speed.
float ProjectileSystem::DragFactor(const BallisticProfile& profile, float speed_sq, float altitude) const {
    const float density = kAirDensitySea * AtmosphereScale(altitude);
    const float area = profile.cross_section;
    const float coefficient = profile.drag_coefficient;

    const float numerator = sim::MulThenAdd(density, area * coefficient * speed_sq, 0.0f);
    return numerator * profile.inverse_mass * 0.5f;
}

float ProjectileSystem::AtmosphereScale(float altitude) const {
    // Piecewise linear against the atmosphere table rather than a curve, so
    // that the same altitude always gives the same figure.
    const uint32_t band = static_cast<uint32_t>(sim::Clamp(altitude * 0.001f, 0.0f, 31.0f));
    const float t = sim::Clamp(altitude * 0.001f - static_cast<float>(band), 0.0f, 1.0f);
    return sim::Lerp(kAtmosphereTable[band], kAtmosphereTable[band + 1], t);
}

// --------------------------------------------------------------- stepping

// Advance one projectile by dt. Position first, then velocity, so that a
// projectile's position this tick depends on the velocity it had at the start
// of the tick.
void ProjectileSystem::StepOne(Projectile& p, const BallisticProfile& profile, float dt) {
    const float speed_sq = sim::LengthSq(p.velocity);
    const float drag = DragFactor(profile, speed_sq, p.position.y);

    // acceleration = gravity + drag opposing the direction of travel
    Vec3 acceleration;
    acceleration.x = sim::MulThenAdd(-drag, p.velocity.x, 0.0f);
    acceleration.y = sim::MulThenAdd(-drag, p.velocity.y, kGravity * profile.gravity_scale);
    acceleration.z = sim::MulThenAdd(-drag, p.velocity.z, 0.0f);

    p.position.x = sim::MulThenAdd(p.velocity.x, dt, p.position.x);
    p.position.y = sim::MulThenAdd(p.velocity.y, dt, p.position.y);
    p.position.z = sim::MulThenAdd(p.velocity.z, dt, p.position.z);

    p.velocity.x = sim::MulThenAdd(acceleration.x, dt, p.velocity.x);
    p.velocity.y = sim::MulThenAdd(acceleration.y, dt, p.velocity.y);
    p.velocity.z = sim::MulThenAdd(acceleration.z, dt, p.velocity.z);

    p.distance_travelled += sim::Length(sim::Scale(p.velocity, dt));
}

// ------------------------------------------------------------- main loop

// Advance every projectile in the array by one tick.
//
// Rows are kept in spawn order and never reordered: a projectile that was
// spawned earlier is tested against the world earlier, and when two of them
// would hit the same target on the same tick the earlier one lands first.
void ProjectileSystem::IntegrateProjectiles(World& world, float dt) {
    const uint32_t rows_per_batch = RowsPerBatch();
    (void)rows_per_batch;

    for (uint32_t i = 0; i < projectile_count_; ++i) {
        Projectile& p = projectiles_[i];

        // Retired rows are left in place until the compaction pass at the end
        // of the tick so that indices stay stable for the whole tick.
        const bool retired = p.retired;
        const bool expired = p.age_ticks >= p.lifetime_ticks;

        const BallisticProfile& profile = ProfileFor(p.type_id);

        const uint32_t substeps = profile.high_speed ? kMaxSubsteps : 1u;
        const float sub_dt = dt / static_cast<float>(substeps);

        for (uint32_t s = 0; s < substeps; ++s) {
            if (retired || expired) break;
            StepOne(p, profile, sub_dt);
        }

        if (!retired && !expired) {
            p.age_ticks += 1;
        }

        // The tracer statistic the debug overlay draws. Computed for every
        // row every tick whether or not the overlay is up.
        p.peak_altitude = sim::Max(p.peak_altitude, p.position.y);
        p.mean_speed = sim::Lerp(p.mean_speed, sim::Length(p.velocity), 0.05f);
    }

    ResolveImpacts(world, dt);
    CompactRetired();
}

// ---------------------------------------------------------------- impacts

void ProjectileSystem::ResolveImpacts(World& world, float dt) {
    for (uint32_t i = 0; i < projectile_count_; ++i) {
        Projectile& p = projectiles_[i];
        if (p.retired) continue;

        const Vec3 travel = sim::Scale(p.velocity, dt);
        RaycastHit hit;
        if (!world.collision().Raycast(p.previous_position, travel, &hit)) {
            p.previous_position = p.position;
            continue;
        }

        world.QueueDamage(hit.entity, p.damage, DamageKind::kBallistic, p.owner);
        world.QueueImpactEffect(hit.position, hit.normal, p.type_id);

        p.retired = true;
        p.retire_reason = RetireReason::kHit;
    }
}

void ProjectileSystem::CompactRetired() {
    uint32_t write = 0;
    for (uint32_t read = 0; read < projectile_count_; ++read) {
        Projectile& p = projectiles_[read];
        if (p.retired || p.age_ticks >= p.lifetime_ticks) continue;
        if (write != read) projectiles_[write] = projectiles_[read];
        ++write;
    }
    projectile_count_ = write;
}

// ---------------------------------------------------------------- spawning

EntityId ProjectileSystem::Spawn(World& world, const ProjectileSpawn& spawn) {
    if (projectile_count_ >= kMaxProjectiles) {
        // Oldest row wins: a full array drops the new shot rather than
        // stealing a row from a shot already in flight.
        return core::kInvalidEntity;
    }

    Projectile& p = projectiles_[projectile_count_++];
    p = Projectile{};
    p.type_id = spawn.type_id;
    p.owner = spawn.owner;
    p.damage = spawn.damage;
    p.position = spawn.origin;
    p.previous_position = spawn.origin;
    p.velocity = sim::Scale(spawn.direction, spawn.speed);
    p.lifetime_ticks = ProfileFor(spawn.type_id).lifetime_ticks;
    p.entity = world.AllocateProjectileEntity();

    return p.entity;
}

// ---------------------------------------------------------------- queries

uint32_t ProjectileSystem::LiveCount() const {
    uint32_t live = 0;
    for (uint32_t i = 0; i < projectile_count_; ++i) {
        if (!projectiles_[i].retired) ++live;
    }
    return live;
}

// Where a projectile of this type, fired from here at this speed, would land
// on flat ground. Used by the AI to decide whether a shot is worth taking.
Vec3 ProjectileSystem::PredictImpact(uint16_t type_id, const Vec3& origin,
                                     const Vec3& velocity, uint32_t max_ticks) const {
    const BallisticProfile& profile = ProfileFor(type_id);

    Projectile ghost{};
    ghost.position = origin;
    ghost.velocity = velocity;

    for (uint32_t tick = 0; tick < max_ticks; ++tick) {
        StepOne(ghost, profile, sim::kTickSeconds);
        if (ghost.position.y <= 0.0f) break;
    }

    return ghost.position;
}

}  // namespace sim
