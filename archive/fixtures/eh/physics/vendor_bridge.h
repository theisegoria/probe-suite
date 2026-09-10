// vendor_bridge.h
//
// Bridge between the engine rigid body layer and the vendor physics SDK.
//
// The engine owns the broad phase, the character controller and the contact
// solver for anything that touches gameplay. The vendor SDK is used for the
// destruction simulation and for the ragdolls, both of which are presentation
// only and are therefore allowed to be non deterministic.
//
// Copyright (c) Northlight Interactive. Internal physics header.

#pragma once

#include <cstdint>

namespace physx {
class PxScene;
class PxRigidDynamic;
class PxMaterial;
class PxPhysics;
}  // namespace physx

namespace nl {
namespace physics {

struct RagdollDesc {
    float total_mass_kg = 70.0f;
    float linear_damping = 0.05f;
    float angular_damping = 0.05f;
    float max_depenetration_velocity = 3.0f;
    bool  enable_ccd = false;
};

class VendorBridge {
public:
    bool Initialise();
    void Shutdown();

    physx::PxRigidDynamic* CreateRagdollBody(const RagdollDesc& desc);
    void StepPresentationScene(float dt);

private:
    physx::PxPhysics* physics_ = nullptr;
    physx::PxScene*   scene_ = nullptr;
    physx::PxMaterial* default_material_ = nullptr;
};

}  // namespace physics
}  // namespace nl
