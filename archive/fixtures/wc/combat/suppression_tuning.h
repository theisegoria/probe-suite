// combat/suppression_tuning.h
//
// Tunables for the suppression system. Near misses raise a suppression value
// on the actor being shot at, which widens their aim cone, darkens the screen
// edges on the presenting client, and biases the AI toward taking cover.
//
// Suppression is simulation state, not presentation state: it changes the aim
// cone, so it has to be identical on every peer.

#ifndef COMBAT_SUPPRESSION_TUNING_H
#define COMBAT_SUPPRESSION_TUNING_H

namespace combat {

// Radius, in metres, at which a passing round stops contributing suppression
// at all. Rounds that pass further away than this are ignored by the
// suppression pass entirely, which is what keeps the per tick cost bounded on
// maps with heavy long range fire.
constexpr float kSuppressionOuterRadiusM = 10.0f;

// Inside this radius the round contributes the full per round intensity. The
// profile between inner and outer is the same 1 - t^2 curve the splash damage
// falloff uses, with t measured against the outer radius.
constexpr float kSuppressionInnerRadiusM = 2.5f;

// Intensity contributed by a single round passing at the inner radius. The
// accumulated value is clamped to 1.0 before it is read by anything else.
constexpr float kSuppressionPerRound = 0.35f;

// Suppression bleeds off at this rate once rounds stop passing. Chosen so a
// player who breaks contact recovers over roughly a second and a quarter.
constexpr float kSuppressionDecayPerSecond = 0.8f;

// Maximum number of suppressing sources tracked per actor in a tick. Beyond
// this the extra sources are dropped rather than accumulated, so that a single
// automatic weapon cannot dominate the tick budget.
constexpr int kSuppressionMaxSources = 8;

// Aim cone widening at full suppression, in degrees. Applied additively to the
// weapon's base cone.
constexpr float kSuppressionMaxConeDegrees = 4.0f;

// AI reads this threshold to decide whether to break and seek cover.
constexpr float kSuppressionCoverSeekThreshold = 0.6f;

}  // namespace combat

#endif  // COMBAT_SUPPRESSION_TUNING_H
