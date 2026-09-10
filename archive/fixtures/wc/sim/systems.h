// sim/systems.h
//
// The set of simulation systems and their stable identifiers.
//
// Ids are serialised into replays and into the per system profiling stream, so
// the numeric values are frozen. New systems are appended; nothing is ever
// renumbered. The order of this enum is not the order systems run in: the
// running order belongs to whichever pipeline is driving the tick.

#ifndef SIM_SYSTEMS_H
#define SIM_SYSTEMS_H

#include <cstdint>

namespace sim {

enum class SystemId : uint8_t {
    kInputApply = 0,        // consumes the input ring for this tick
    kAbilityTick = 1,       // cooldowns, channels, ability state machines
    kProjectileIntegrate = 2,  // advances projectiles and does their sweeps
    kPhysicsStep = 3,       // rigid body broadphase, solve, integrate
    kContactEvents = 4,     // turns solver contacts into gameplay events
    kDamageResolve = 5,     // applies queued damage and death flags
    kDeathReap = 6,         // destroys entities flagged dead this tick
    kTransformSync = 7,     // publishes transforms for presentation
    kAnimationDrive = 8,    // presentation only, never authoritative
    kVfxSpawn = 9,          // presentation only, never authoritative
    kSampleEditorInput = 10,  // editor preview only
    kCount = 11,
};

// Stable names, used by the profiler overlay and by the replay diff tool.
inline const char* SystemName(SystemId id) {
    switch (id) {
        case SystemId::kInputApply: return "InputApply";
        case SystemId::kAbilityTick: return "AbilityTick";
        case SystemId::kProjectileIntegrate: return "ProjectileIntegrate";
        case SystemId::kPhysicsStep: return "PhysicsStep";
        case SystemId::kContactEvents: return "ContactEvents";
        case SystemId::kDamageResolve: return "DamageResolve";
        case SystemId::kDeathReap: return "DeathReap";
        case SystemId::kTransformSync: return "TransformSync";
        case SystemId::kAnimationDrive: return "AnimationDrive";
        case SystemId::kVfxSpawn: return "VfxSpawn";
        case SystemId::kSampleEditorInput: return "SampleEditorInput";
        case SystemId::kCount: return "Count";
    }
    return "Unknown";
}

// True for systems that may write simulation state. Presentation only systems
// are allowed to run in a different order, or not at all, on a given peer.
inline bool IsAuthoritative(SystemId id) {
    switch (id) {
        case SystemId::kAnimationDrive:
        case SystemId::kVfxSpawn:
        case SystemId::kSampleEditorInput:
            return false;
        default:
            return true;
    }
}

}  // namespace sim

#endif  // SIM_SYSTEMS_H
