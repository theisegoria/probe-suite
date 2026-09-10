// engine/sim/ai_perception.cpp
//
// What an AI actor knows about the world.
//
// Perception runs on a rota: each actor re-evaluates a slice of its knowledge
// every few ticks rather than all of it every tick, and the rota is driven off
// the actor's slot so the same actors think on the same ticks on every peer.
//
// Everything here is simulation. The target an actor picks decides where it
// walks and what it shoots, so two peers that pick differently have already
// desynced by the time anything is drawn.

#include "sim/ai_perception.h"

#include <algorithm>
#include <cstdint>

#include "core/types.h"
#include "math/sim_math.h"
#include "render/view_state.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;

constexpr uint32_t kPerceptionPeriodTicks = 6;
constexpr uint32_t kMaxCandidates         = 32;
constexpr uint32_t kRaycastBudgetPerTick  = 64;
constexpr float    kHearingRadius         = 35.0f;
constexpr float    kPeripheralDot         = 0.15f;
constexpr Q16      kThreatWeight          = core::kQ16One * 2;
constexpr Q16      kProximityWeight       = core::kQ16One;
constexpr Q16      kRecencyWeight         = core::kQ16One / 2;

// Eye height above the actor's feet, per size class.
constexpr float kEyeHeight[] = {0.4f, 1.2f, 1.7f, 2.6f};

Vec3 EyePosition(const Actor& actor) {
    const uint32_t size_class = actor.size_class < 4 ? actor.size_class : 3u;
    Vec3 eye = actor.transform.position;
    eye.y += kEyeHeight[size_class];
    return eye;
}

}  // namespace

// -------------------------------------------------------------- line of sight

// A single ray against the simulation collision world, from one actor's eye
// to another's centre of mass. This is the only kind of visibility the
// simulation has: it knows nothing about what any player is looking at.
//
// Costs one broadphase walk plus a handful of triangle tests, which is why
// callers go through the budget below rather than calling it in a loop.
bool AiPerception::HasLineOfSight(const World& world, const Actor& from, const Actor& to) const {
    const Vec3 eye = EyePosition(from);
    const Vec3 target = sim::Add(to.transform.position, Vec3{0.0f, kEyeHeight[1] * 0.5f, 0.0f});
    const Vec3 delta = sim::Sub(target, eye);

    RaycastHit hit;
    if (!world.collision().Raycast(eye, delta, &hit)) {
        return true;   // nothing in the way
    }

    // A hit on the target itself still counts as seeing it.
    return hit.entity == to.entity;
}

// Actors get a fixed number of rays per tick between them, spent in slot
// order, so that a busy tick starves the same actors on every machine.
bool AiPerception::TrySpendRaycast() {
    if (raycasts_this_tick_ >= kRaycastBudgetPerTick) return false;
    ++raycasts_this_tick_;
    return true;
}

// Is the target inside this actor's view cone, ignoring geometry.
bool AiPerception::WithinViewCone(const Actor& from, const Actor& to) const {
    const Vec3 forward = FacingVector(from.transform.facing_turns);
    const Vec3 delta = sim::Sub(to.transform.position, from.transform.position);
    const float length_sq = sim::LengthSq(delta);
    if (length_sq < 1.0e-4f) return true;

    const Vec3 direction = sim::Scale(delta, 1.0f / sim::Length(delta));
    return sim::Dot(forward, direction) >= kPeripheralDot;
}

// ------------------------------------------------------------------ hearing

// Gunfire, footsteps and explosions are queued as sound events by whatever
// made them, in the order they happened, and are drained here.
void AiPerception::ProcessSoundEvents(World& world) {
    for (uint32_t i = 0; i < world.sound_event_count(); ++i) {
        const SoundEvent& event = world.sound_event(i);

        for (core::Slot slot = 0; slot < world.actor_count(); ++slot) {
            Actor& listener = world.actor(slot);
            if (!listener.alive || !listener.has_ai) continue;
            if (listener.team == event.team) continue;

            const Vec3 delta = sim::Sub(event.position, listener.transform.position);
            const float radius = kHearingRadius * event.loudness;
            if (sim::LengthSq(delta) > radius * radius) continue;

            AiKnowledge& knowledge = world.knowledge_of(listener.entity);
            knowledge.last_heard_position = event.position;
            knowledge.last_heard_tick = world.tick;
            knowledge.alertness = std::min<uint8_t>(knowledge.alertness + 40, 255);
        }
    }
}

// ------------------------------------------------------------- target scoring

// Score a candidate target for this actor. Higher is better. Ties are broken
// by entity id in ChooseTarget below.
Q16 AiPerception::ScoreTarget(const World& world, const Actor& self, const Actor& candidate) const {
    const Vec3 delta = sim::Sub(candidate.transform.position, self.transform.position);
    const float distance_sq = sim::LengthSq(delta);

    const float range = world.def_of(self.entity).engagement_range;
    if (distance_sq > range * range) return 0;

    // Closer is better, as a Q16 fraction of the engagement range.
    const float normalised = 1.0f - sim::Clamp(distance_sq / (range * range), 0.0f, 1.0f);
    Q16 score = static_cast<Q16>(normalised * static_cast<float>(kProximityWeight));

    // Whoever has been hurting us most gets attention.
    score += core::MulQ16(world.threat().ThreatOf(candidate.entity), kThreatWeight);

    // Something we saw recently is a better bet than something we have not.
    const AiKnowledge& knowledge = world.knowledge_of(self.entity);
    if (knowledge.last_target == candidate.entity) {
        const uint32_t age = world.tick - knowledge.last_target_tick;
        if (age < 120) {
            score += core::MulQ16(core::FromInt(static_cast<int32_t>(120 - age)) / 120, kRecencyWeight);
        }
    }

    // Wounded targets are worth finishing.
    if (candidate.health_q16 < candidate.max_health_q16 / 4) {
        score += core::kQ16One / 4;
    }

    return score;
}

EntityId AiPerception::ChooseTarget(World& world, const Actor& self) const {
    EntityId best = core::kInvalidEntity;
    Q16 best_score = 0;

    for (core::Slot slot = 0; slot < world.actor_count(); ++slot) {
        const Actor& candidate = world.actor(slot);
        if (!candidate.alive) continue;
        if (candidate.team == self.team) continue;
        if (candidate.entity == self.entity) continue;

        const Q16 score = ScoreTarget(world, self, candidate);
        if (score <= 0) continue;

        if (score > best_score || (score == best_score && candidate.entity < best)) {
            best_score = score;
            best = candidate.entity;
        }
    }

    return best;
}

// ---------------------------------------------------------------- cover

// Cover selection already uses the line of sight test: a cover slot is good
// when the actor can reach it and the current target cannot see into it.
CoverSlot AiPerception::ChooseCover(World& world, const Actor& self, EntityId threat) {
    CoverSlot best{};
    best.valid = false;
    Q16 best_score = 0;

    const Actor& threat_actor = world.actor_by_entity(threat);

    for (uint32_t i = 0; i < world.cover_slot_count(); ++i) {
        const CoverSlot& slot = world.cover_slot(i);
        if (slot.occupant != core::kInvalidEntity && slot.occupant != self.entity) continue;

        const Vec3 delta = sim::Sub(slot.position, self.transform.position);
        const float distance_sq = sim::LengthSq(delta);
        if (distance_sq > 40.0f * 40.0f) continue;

        if (!TrySpendRaycast()) break;

        Actor probe = self;
        probe.transform.position = slot.position;
        if (HasLineOfSight(world, threat_actor, probe)) continue;

        const Q16 score = core::DivQ16(core::kQ16One,
                                       core::FromInt(static_cast<int32_t>(distance_sq)) + core::kQ16One);
        if (score > best_score) {
            best_score = score;
            best = slot;
            best.valid = true;
        }
    }

    return best;
}

// ------------------------------------------------------------------ tick

void AiPerception::Tick(World& world) {
    raycasts_this_tick_ = 0;

    ProcessSoundEvents(world);

    for (core::Slot slot = 0; slot < world.actor_count(); ++slot) {
        Actor& self = world.actor(slot);
        if (!self.alive || !self.has_ai) continue;

        // The rota: an actor thinks on the tick its slot comes up.
        if (((world.tick + slot) % kPerceptionPeriodTicks) != 0) continue;

        AiKnowledge& knowledge = world.knowledge_of(self.entity);

        const EntityId chosen = ChooseTarget(world, self);
        if (chosen != knowledge.last_target) {
            knowledge.previous_target = knowledge.last_target;
            knowledge.last_target = chosen;
            knowledge.last_target_tick = world.tick;
        }

        if (knowledge.alertness > 0) knowledge.alertness -= 1;
    }
}

// ------------------------------------------------------------------ debug

// Draws the perception state for the actor the designer has selected in the
// AI debugger. Editor only, and it never writes anything back.
void AiPerception::DebugDraw(const World& world, EntityId selected) const {
    if (selected == core::kInvalidEntity) return;

    const Actor& self = world.actor_by_entity(selected);
    const AiKnowledge& knowledge = world.knowledge_of(selected);

    render::DebugLines().AddSphere(self.transform.position, 0.5f, 0xFF00FF00u);

    if (knowledge.last_target != core::kInvalidEntity) {
        const Actor& target = world.actor_by_entity(knowledge.last_target);
        render::DebugLines().Add(EyePosition(self), target.transform.position, 0xFFFF0000u);
    }

    if (knowledge.last_heard_tick != 0) {
        render::DebugLines().AddSphere(knowledge.last_heard_position, 0.35f, 0xFF0000FFu);
    }
}

}  // namespace sim
