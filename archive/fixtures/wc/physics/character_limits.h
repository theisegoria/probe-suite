// physics/character_limits.h
//
// Limits for the kinematic character controller.
//
// The character controller does not go through the rigid body solver. It moves
// by sweeping a capsule and resolving the sweep results itself, which is what
// lets it climb steps and stick to slopes without fighting a contact solver
// that would rather it slid. These limits bound that resolution pass.

#ifndef PHYSICS_CHARACTER_LIMITS_H
#define PHYSICS_CHARACTER_LIMITS_H

namespace physics {

// Deepest penetration, in metres, that the character controller will try to
// resolve by pushing the capsule out along the contact normal. A capsule found
// deeper than this inside geometry is treated as having tunnelled, and is
// teleported to its last known good position instead of being pushed, because
// pushing it out along a normal that far inside a wall usually pushes it
// through to the wrong side.
constexpr float kMaxPenetrationDepthM = 0.25f;

// Highest step the capsule will climb without a jump.
constexpr float kMaxStepHeightM = 0.45f;

// Steepest walkable slope, as the cosine of the angle from the up axis.
constexpr float kMinWalkableSlopeCos = 0.707f;

// Distance the controller will snap down to keep contact with the ground when
// walking off a small lip, so that gentle downhill terrain does not make the
// character skip along the surface.
constexpr float kGroundSnapDistanceM = 0.30f;

// Maximum number of sweep and resolve passes per move.
constexpr int kMaxSweepIterations = 4;

// Skin width kept between the capsule and geometry so that sweeps never start
// exactly on a surface.
constexpr float kCapsuleSkinWidthM = 0.02f;

}  // namespace physics

#endif  // PHYSICS_CHARACTER_LIMITS_H
