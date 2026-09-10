// physics/contact_rows.cpp
//
// Current contact row maths.
//
// Structurally the same as the 2.6 path in contact_rows_v1.cpp. The only
// behavioural change in 2.7 is the restitution slop, which moved up so that
// contacts resting on moving platforms stop re triggering restitution.

#include "physics/contact_rows.h"

#include "physics/island.h"

namespace physics {
namespace {

float Max(float a, float b) {
    return a > b ? a : b;
}

float Min(float a, float b) {
    return a < b ? a : b;
}

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

}  // namespace physics
