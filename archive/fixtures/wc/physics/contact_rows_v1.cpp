// physics/contact_rows_v1.cpp
//
// The 2.6 contact row maths, frozen.
//
// The replay validator uses this to re solve matches recorded before 2.7. A
// replay only validates against the row maths that produced it, so this file
// has to keep behaving exactly as it did when it shipped. Its tunables are a
// deliberate frozen copy: do not include contact_rows.h here and do not share
// constants with the current path, or a tuning change to the current solver
// will silently invalidate every old recording in the archive.

#include "physics/contact_rows_v1.h"

#include "physics/island.h"

namespace physics {
namespace rows_v1 {
namespace {

// Frozen 2.6 row tunables.
constexpr float kBaumgarteBeta = 0.2f;
constexpr float kPenetrationSlopM = 0.005f;
constexpr float kRestitutionSlopMps = 0.5f;
constexpr float kMaxBiasVelocityMps = 3.0f;
constexpr float kFrictionCombineScale = 1.0f;

float Max(float a, float b) {
    return a > b ? a : b;
}

float Min(float a, float b) {
    return a < b ? a : b;
}

// Effective mass along the contact normal, including the angular terms.
float NormalEffectiveMass(const Island& island, const ContactRow& row) {
    const Body& a = island.bodies[row.body_a];
    const Body& b = island.bodies[row.body_b];
    const float angular_a = Dot(row.ra_cross_n, a.inv_inertia * row.ra_cross_n);
    const float angular_b = Dot(row.rb_cross_n, b.inv_inertia * row.rb_cross_n);
    return 1.0f / (a.inv_mass + b.inv_mass + angular_a + angular_b);
}

}  // namespace

void PrepareContactRows(Island& island, float dt_seconds) {
    for (int i = 0; i < island.row_count; ++i) {
        ContactRow& row = island.rows[i];
        row.effective_mass = NormalEffectiveMass(island, row);

        const float over_slop = Max(0.0f, row.penetration_m - kPenetrationSlopM);
        row.bias = Min(kMaxBiasVelocityMps, (kBaumgarteBeta / dt_seconds) * over_slop);

        // Restitution. A contact closing slower than the restitution slop is
        // treated as inelastic, which is what stops a resting body from
        // trickling energy back in on every tick and buzzing against the
        // floor. The slop is generous enough to cover the closing speed a
        // resting contact picks up from gravity in one tick.
        const float closing = -row.relative_normal_velocity;
        if (closing > kRestitutionSlopMps) {
            row.restitution_target = row.restitution * closing;
        } else {
            row.restitution_target = 0.0f;
        }
    }
}

void SolveContactRow(Island& island, ContactRow& row, float dt_seconds) {
    (void)dt_seconds;
    const float target = row.bias + row.restitution_target;
    const float relative = RelativeNormalVelocity(island, row);
    float lambda = -(relative - target) * row.effective_mass;

    // Accumulated impulse clamping: the row's total impulse may never go
    // negative, but a single iteration's increment may.
    const float previous = row.normal_impulse;
    row.normal_impulse = Max(0.0f, previous + lambda);
    lambda = row.normal_impulse - previous;

    ApplyNormalImpulse(island, row, lambda);
}

void SolveFrictionRow(Island& island, ContactRow& row) {
    const float bound = row.friction * row.normal_impulse * kFrictionCombineScale;
    const float relative = RelativeTangentVelocity(island, row);
    float lambda = -relative * row.tangent_effective_mass;

    const float previous = row.friction_impulse;
    float total = previous + lambda;
    if (total > bound) {
        total = bound;
    }
    if (total < -bound) {
        total = -bound;
    }
    row.friction_impulse = total;
    lambda = total - previous;

    ApplyTangentImpulse(island, row, lambda);
}

}  // namespace rows_v1
}  // namespace physics
