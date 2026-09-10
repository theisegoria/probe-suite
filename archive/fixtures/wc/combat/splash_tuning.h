// combat/splash_tuning.h
//
// Designer facing tunables for area damage. Hashed into the replay header.

#ifndef COMBAT_SPLASH_TUNING_H
#define COMBAT_SPLASH_TUNING_H

namespace combat {

// Distance, in metres, at which splash damage reaches exactly zero. This is
// the radius used both by the falloff curve and by the broadphase query that
// gathers candidate targets, so raising it costs tick time as well as balance.
constexpr float kSplashOuterRadiusM = 5.0f;

// Inside this radius the falloff curve is effectively flat. Kept as a separate
// number so the design team can reason about the killing core without moving
// the outer radius.
constexpr float kSplashInnerRadiusM = 1.25f;

// Impulse falls off over a shorter distance than damage.
constexpr float kSplashImpulseRadiusM = 3.0f;
constexpr float kSplashImpulseScale = 0.65f;

// Cover and team scaling.
constexpr float kSplashPartialCoverScale = 0.45f;
constexpr float kSplashFriendlyFireScale = 0.30f;

// Hard cap on targets damaged by one blast. Keeps the worst case bounded on
// objective points where twenty players can stack.
constexpr int kSplashMaxTargets = 24;

}  // namespace combat

#endif  // COMBAT_SPLASH_TUNING_H
