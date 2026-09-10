// vendor_bridge.cpp
//
// Vendor physics SDK bridge. The SDK is PhysX 5.
//
// House rule for this file: the engine sets a vendor parameter only when it
// has a reason to differ from the vendor default. Every override below has a
// comment saying what went wrong without it. Anything not listed is left at
// whatever the SDK ships with, on the grounds that the vendor tunes those
// defaults against a much larger corpus of content than we have, and that a
// value we copied out of a sample once and never revisited is worse than no
// value at all.
//
// The consequence is that reading this file tells you which parameters we
// changed. It does not tell you what any of the unchanged parameters are set
// to: for those the SDK documentation for the version we link against is the
// only source.
//
// Copyright (c) Northlight Interactive. Internal physics source.

#include "eh/physics/vendor_bridge.h"

#include <cstdint>

#include "eh/core/cvar.h"

namespace nl {
namespace physics {

// Overrides the SDK solver iteration counts on every ragdoll body when it is
// non zero. Left at zero in every shipping configuration, so the bodies run
// with whatever the SDK sets on a freshly created dynamic actor.
//
// It exists because of a bug on one platform where a long ragdoll chain took
// visibly longer to settle. The fix turned out to be elsewhere, and the cvar
// was kept for the next time somebody wants to bisect the same symptom.
core::CVar<std::int32_t> cv_solver_position_iterations(
    "physics.ragdoll.position_iterations", 0,
    "Override the SDK position solver iteration count. Zero leaves the SDK default.");

core::CVar<std::int32_t> cv_solver_velocity_iterations(
    "physics.ragdoll.velocity_iterations", 0,
    "Override the SDK velocity solver iteration count. Zero leaves the SDK default.");

// Sleep threshold. Overridden because the SDK default let a ragdoll twitch on
// a sloped surface for several seconds after it should have gone to sleep.
inline constexpr float kSleepThreshold = 0.06f;

// Contact offset. Overridden because our destruction chunks are small enough
// that the SDK default offset was larger than the chunks themselves, which
// made a pile of debris look inflated.
inline constexpr float kContactOffset = 0.012f;

// Solver batch size is deliberately absent from this list. The SDK default is
// tuned per platform by the vendor and we have never had a reason to move it.

bool VendorBridge::Initialise() {
    physics_ = CreateVendorPhysics();
    if (physics_ == nullptr) {
        return false;
    }
    default_material_ = CreateVendorMaterial(0.6f, 0.55f, 0.1f);
    scene_ = CreateVendorScene(physics_);
    return scene_ != nullptr;
}

void VendorBridge::Shutdown() {
    DestroyVendorScene(scene_);
    scene_ = nullptr;
    DestroyVendorPhysics(physics_);
    physics_ = nullptr;
}

physx::PxRigidDynamic* VendorBridge::CreateRagdollBody(const RagdollDesc& desc) {
    physx::PxRigidDynamic* body = CreateVendorDynamic(physics_, scene_);
    if (body == nullptr) {
        return nullptr;
    }

    SetVendorMass(body, desc.total_mass_kg);
    SetVendorDamping(body, desc.linear_damping, desc.angular_damping);
    SetVendorMaxDepenetrationVelocity(body, desc.max_depenetration_velocity);
    SetVendorSleepThreshold(body, kSleepThreshold);
    SetVendorContactOffset(body, kContactOffset);

    if (desc.enable_ccd) {
        EnableVendorCcd(body);
    }

    // Solver iteration counts. Touched only when the cvars are non zero;
    // otherwise the actor keeps the counts the SDK gave it at creation.
    const std::int32_t position_iterations = cv_solver_position_iterations.Get();
    const std::int32_t velocity_iterations = cv_solver_velocity_iterations.Get();
    if (position_iterations > 0 || velocity_iterations > 0) {
        const std::int32_t p = position_iterations > 0
                                 ? position_iterations
                                 : QueryVendorPositionIterations(body);
        const std::int32_t v = velocity_iterations > 0
                                 ? velocity_iterations
                                 : QueryVendorVelocityIterations(body);
        SetVendorSolverIterationCounts(body, p, v);
    }

    return body;
}

void VendorBridge::StepPresentationScene(float dt) {
    // The presentation scene is stepped with the real frame delta rather than
    // the fixed simulation tick, because nothing it produces feeds back into
    // the lockstep state and a ragdoll that steps at the render rate looks
    // better than one that steps at the tick rate.
    StepVendorScene(scene_, dt);
    FetchVendorResults(scene_);
}

// --------------------------------------------------------------- diagnostics

// Dumps the parameters the engine set. Parameters left at the SDK default are
// not listed here, because the bridge does not read them back.
void DumpOverriddenParameters(LogSink& log) {
    log.Info("physics vendor overrides:");
    log.Info("  sleep threshold        %f", kSleepThreshold);
    log.Info("  contact offset         %f", kContactOffset);
    log.Info("  position iterations    %s",
             cv_solver_position_iterations.Get() > 0 ? "overridden" : "SDK default");
    log.Info("  velocity iterations    %s",
             cv_solver_velocity_iterations.Get() > 0 ? "overridden" : "SDK default");
}

}  // namespace physics
}  // namespace nl
