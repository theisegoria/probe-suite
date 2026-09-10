// contact_solver.cpp
//
// Sequential impulse solver with Baumgarte stabilisation and warm starting.
//
// Four things happen to a contact every tick, in this order, and each of them
// changes the numbers the next one sees:
//
//   1. Prepare. The effective mass and the positional bias are computed once
//      and cached on the contact. The bias is where Baumgarte lives.
//   2. Warm start. The impulse accumulated on this contact last tick is
//      applied immediately, before any iteration runs. The accumulator is not
//      reset: it keeps its value and the iterations add to it.
//   3. Iterate. Each velocity iteration computes a candidate impulse, scales
//      it by the relaxation factor, adds it to the accumulator, clamps the
//      accumulator, and applies whatever the clamp actually allowed through.
//   4. Nothing. The accumulator is left alone so the next tick can warm start
//      from it.
//
// Two details in step three catch people out. The relaxation factor is
// applied to the candidate impulse, so a single iteration only closes part of
// the velocity error and three iterations are not equivalent to one. And the
// clamp is on the accumulator rather than on the increment, so the impulse
// that gets applied is the difference between the clamped accumulator and its
// previous value rather than the candidate.
//
// Copyright (c) Northlight Interactive. Internal physics source.

#include "eh/physics/contact_solver.h"

#include "eh/math/scalar.h"

namespace nl {
namespace physics {

using math::Max;
using math::Min;

// Baumgarte coefficient. The fraction of the remaining penetration that the
// solver tries to remove per second of simulated time.
inline constexpr float kBaumgarte = 0.2f;

// Penetration below this is left alone. Chasing the last fraction of a
// millimetre makes resting stacks jitter.
inline constexpr float kLinearSlop = 0.005f;

// Ceiling on the positional bias velocity. Without it a deep overlap, which
// is usually a teleport or a spawn inside geometry, launches the body.
inline constexpr float kMaxBiasVelocity = 0.4f;

// Successive over relaxation factor. Below one, so each iteration takes only
// part of the step it would otherwise take. Full steps make a stack of boxes
// ring; partial steps converge slower but settle.
inline constexpr float kRelaxation = 0.5f;

// Velocity iterations per tick.
inline constexpr int kVelocityIterations = 3;

// Approach speeds below this get no restitution. A resting body has a tiny
// negative normal velocity every tick from gravity, and bouncing it back is
// how a box on the floor develops a hum.
inline constexpr float kRestitutionThreshold = 5.0f;

// ------------------------------------------------------------------ prepare

void PrepareContact(ContactPoint& c, const RigidBody& a, const RigidBody& b,
                    float dt) {
    const float inv_mass_sum = a.inverse_mass + b.inverse_mass;
    c.effective_mass = inv_mass_sum > 0.0f ? (1.0f / inv_mass_sum) : 0.0f;

    // Baumgarte. The slop is subtracted first, so a contact that is only
    // barely overlapping produces no bias at all.
    const float correctable = Max(c.penetration - kLinearSlop, 0.0f);
    const float raw_bias = (kBaumgarte / dt) * correctable;
    c.bias = Min(raw_bias, kMaxBiasVelocity);

    // Restitution is evaluated once, against the approach speed as it stands
    // before any impulse has been applied this tick.
    const float approach = Dot(b.linear_velocity - a.linear_velocity, c.normal);
    if (-approach >= kRestitutionThreshold) {
        c.restitution_target = -b.restitution * approach;
    } else {
        c.restitution_target = 0.0f;
    }
}

// --------------------------------------------------------------- warm start

// Apply the impulse this contact accumulated last tick. The accumulator keeps
// its value: the iterations below continue from it rather than from zero.
void WarmStart(ContactPoint& c, RigidBody& a, RigidBody& b) {
    const Vec3 p = c.normal * c.normal_impulse;
    a.linear_velocity = a.linear_velocity - p * a.inverse_mass;
    b.linear_velocity = b.linear_velocity + p * b.inverse_mass;
}

// ----------------------------------------------------------------- iterate

void SolveVelocity(ContactPoint& c, RigidBody& a, RigidBody& b) {
    const float vn = Dot(b.linear_velocity - a.linear_velocity, c.normal);

    // Candidate impulse, then relaxation.
    const float target = vn - c.bias - c.restitution_target;
    const float candidate = -c.effective_mass * target * kRelaxation;

    // Clamp the accumulator, not the candidate. The contact can only push,
    // never pull, so the accumulator has a floor of zero.
    const float previous = c.normal_impulse;
    c.normal_impulse = Max(previous + candidate, 0.0f);
    const float applied = c.normal_impulse - previous;

    const Vec3 p = c.normal * applied;
    a.linear_velocity = a.linear_velocity - p * a.inverse_mass;
    b.linear_velocity = b.linear_velocity + p * b.inverse_mass;
}

// ------------------------------------------------------------------- driver

void SolveContact(ContactPoint& c, RigidBody& a, RigidBody& b, float dt) {
    PrepareContact(c, a, b, dt);
    WarmStart(c, a, b);
    for (int i = 0; i < kVelocityIterations; ++i) {
        SolveVelocity(c, a, b);
    }
}

// --------------------------------------------------------------- manifolds

// A manifold is solved point by point rather than as a system. Solving them
// in a fixed order is required: two peers that visit the same contact points
// in different orders produce different velocities.
void SolveManifold(ContactPoint* points, int count, RigidBody& a, RigidBody& b,
                   float dt) {
    for (int i = 0; i < count; ++i) {
        PrepareContact(points[i], a, b, dt);
    }
    for (int i = 0; i < count; ++i) {
        WarmStart(points[i], a, b);
    }
    for (int iter = 0; iter < kVelocityIterations; ++iter) {
        for (int i = 0; i < count; ++i) {
            SolveVelocity(points[i], a, b);
        }
    }
}

// Contacts that were not refreshed by the broad phase this tick lose their
// accumulated impulse, otherwise a body would warm start against a surface it
// left several ticks ago.
void ExpireStaleContacts(ContactPoint* points, const bool* refreshed, int count) {
    for (int i = 0; i < count; ++i) {
        if (!refreshed[i]) {
            points[i].normal_impulse = 0.0f;
            points[i].tangent_impulse = 0.0f;
        }
    }
}

}  // namespace physics
}  // namespace nl
