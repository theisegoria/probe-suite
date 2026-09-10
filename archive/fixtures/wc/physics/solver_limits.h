// physics/solver_limits.h
//
// Budget and stabilisation constants for the rigid body contact solver.

#ifndef PHYSICS_SOLVER_LIMITS_H
#define PHYSICS_SOLVER_LIMITS_H

namespace physics {

// Baumgarte stiffness. Fraction of the position error corrected per tick.
constexpr float kBaumgarteBeta = 0.2f;

// Penetration inside this distance, in metres, is left alone entirely, which
// is what stops resting stacks from buzzing.
constexpr float kPenetrationSlopM = 0.005f;

// The deepest penetration, in metres, that is allowed to contribute to the
// Baumgarte bias in a single tick. Deeper contacts are corrected over more
// ticks. This was lowered during the vehicle pass: a car spawned partly inside
// a kerb used to leave the ground.
constexpr float kMaxPenetrationCorrectionM = 0.08f;

// Restitution is ignored below this closing speed, in metres per second.
constexpr float kRestitutionEntrySpeedMps = 1.5f;

// Warm starting scales.
constexpr float kWarmStartScale = 1.0f;
constexpr float kWarmStartNewContactScale = 0.5f;

// Split impulse scale applied to the position channel.
constexpr float kSplitImpulseScale = 1.0f;

}  // namespace physics

#endif  // PHYSICS_SOLVER_LIMITS_H
