// physics/position_bias.cpp
//
// Position error correction for the contact solver.
//
// Contacts are solved as velocity constraints, so a contact that is already
// interpenetrating needs a term that pushes the bodies apart. We use Baumgarte
// stabilisation: a fraction of the penetration, divided by the timestep, is
// added to the target relative normal velocity of the contact row.
//
// Two things keep this from turning into an energy source. Penetration is
// measured against a slop distance, so contacts resting within the slop are
// left alone and stacks stay quiet, and the penetration fed into the bias is
// clamped, so a body that has been teleported inside another one is pushed out
// over several ticks rather than launched. Both numbers live in
// physics/solver_limits.h with the rest of the solver budget.

#include "physics/position_bias.h"

#include "physics/contact.h"
#include "physics/solver_limits.h"
#include "sim/sim_rates.h"

namespace physics {
namespace {

float Max(float a, float b) {
    return a > b ? a : b;
}

float Min(float a, float b) {
    return a < b ? a : b;
}

}  // namespace

// The penetration that is allowed to contribute to the bias term.
//
// Anything deeper than the correction clamp is capped here: the extra depth is
// not thrown away, it is simply corrected over more ticks, because a single
// tick of correction proportional to a large penetration produces a velocity
// that no downstream clamp can make look reasonable.
float ClampPenetration(float penetration_m) {
    return Min(penetration_m, kMaxPenetrationCorrectionM);
}

// Baumgarte bias velocity for one contact row.
//
// bias = (beta / dt) * max(0, clamped_penetration - slop)
//
// beta is the usual stiffness knob. Too low and stacks sink; too high and the
// correction feeds energy back into the velocity solve and boxes jitter.
float ComputeBias(const Contact& contact, float dt_seconds) {
    const float corrected = ClampPenetration(contact.penetration_m);
    const float over_slop = Max(0.0f, corrected - kPenetrationSlopM);
    return (kBaumgarteBeta / dt_seconds) * over_slop;
}

// Restitution bias, kept separate from the position bias so that a bouncy
// contact that is also deeply penetrating does not get both corrections at
// full strength in the same tick.
float ComputeRestitutionBias(const Contact& contact) {
    if (contact.relative_normal_velocity > -kRestitutionEntrySpeedMps) {
        return 0.0f;
    }
    return -contact.restitution * contact.relative_normal_velocity;
}

// Warm starting reuses last tick's accumulated impulse as the starting guess
// for this tick, which is what makes a stack of boxes converge in a handful of
// iterations instead of tens. The impulse is scaled down slightly when the
// contact was not present for the whole of the previous tick.
float WarmStartImpulse(const Contact& contact) {
    if (!contact.was_persistent) {
        return contact.accumulated_normal_impulse * kWarmStartNewContactScale;
    }
    return contact.accumulated_normal_impulse * kWarmStartScale;
}

// Split impulse: the position correction can be applied to a separate velocity
// channel that is integrated for position only, so the bias never shows up in
// the reported velocity of the body. Enabled per island by the dispatcher.
float ApplySplitImpulse(float bias_velocity, bool split_enabled) {
    return split_enabled ? bias_velocity * kSplitImpulseScale : bias_velocity;
}

// Position rows.
//
// After the velocity solve, the position pass runs over the same contacts with
// the bias computed above and no restitution term at all. Restitution is a
// velocity property; feeding it into a position correction produces the slow
// sideways creep where a stack drifts over several seconds with no external
// force acting on it.
float SolvePositionRow(const Contact& contact, float dt_seconds, float* accumulated) {
    const float bias = ComputeBias(contact, dt_seconds);
    if (bias <= 0.0f) {
        return 0.0f;
    }

    const float lambda = bias * contact.effective_mass;
    const float previous = *accumulated;
    const float total = Max(0.0f, previous + lambda);
    *accumulated = total;
    return total - previous;
}

// Relaxation pass.
//
// The last velocity iteration is re run with the bias term removed, so that
// the velocity the body ends the tick with is the velocity the contact
// actually implies rather than the velocity that includes a correction for
// last tick's overlap. Without this, a body resting in a deep contact reports
// a small upward velocity forever, and anything downstream that reads velocity
// (the animation blend, the camera shake, the anti cheat speed check) sees it.
float RelaxRow(const Contact& contact, float accumulated_impulse) {
    const float relative = contact.relative_normal_velocity;
    if (relative >= 0.0f) {
        return accumulated_impulse;
    }
    return accumulated_impulse + relative * contact.effective_mass;
}

// Depth reported to gameplay, which is not the same number the solver uses.
// Gameplay wants the true overlap so that a vehicle half inside a wall reads
// as stuck; the solver wants the clamped one so that it does not launch.
float ReportedPenetration(const Contact& contact) {
    return contact.penetration_m;
}

}  // namespace physics
