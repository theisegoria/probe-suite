// engine/sim/combat_ledger.cpp
//
// The combat ledger: who hit whom, for how much, and when.
//
// Every point of damage the simulation applies is written here before it is
// applied, in the order it was applied. The ledger is what the kill feed, the
// threat model, the end of match scoreboard and the bounty payout all read
// from, so it has to be a faithful record rather than a summary.
//
// The ring holds five thousand records, which at the damage rates a late game
// fight produces is around forty seconds of history.

#include "sim/combat_ledger.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/types.h"
#include "math/sim_math.h"
#include "sim/world.h"

namespace sim {
namespace {

using core::EntityId;
using core::Q16;
using core::TickIndex;

constexpr uint32_t kRingCapacity        = 5000;
constexpr uint32_t kBountyWindowTicks   = 300;   // five seconds at the fixed rate
constexpr uint32_t kThreatWindowTicks   = 600;
constexpr uint32_t kMaxThreatRows       = 8;
constexpr Q16 kMinRecordedDamage        = core::kQ16One / 64;

}  // namespace

// ---------------------------------------------------------------- writing

void CombatLedger::Record(TickIndex tick, EntityId attacker, EntityId victim,
                          Q16 amount, DamageKind kind) {
    if (amount < kMinRecordedDamage) return;

    DamageRecord& record = ring_[write_cursor_ % kRingCapacity];
    record.tick = tick;
    record.attacker = attacker;
    record.victim = victim;
    record.amount = amount;
    record.kind = kind;
    record.sequence = write_cursor_;

    ++write_cursor_;
    if (record_count_ < kRingCapacity) ++record_count_;
}

void CombatLedger::Clear() {
    write_cursor_ = 0;
    record_count_ = 0;
}

// Oldest surviving record first. Callers that care about order use this
// rather than walking the ring array directly, because the ring wraps.
uint32_t CombatLedger::OldestIndex() const {
    return write_cursor_ >= kRingCapacity ? (write_cursor_ - kRingCapacity) : 0;
}

const DamageRecord& CombatLedger::At(uint32_t sequence) const {
    return ring_[sequence % kRingCapacity];
}

// ---------------------------------------------------------------- reading

Q16 CombatLedger::TotalDamageFrom(EntityId attacker, EntityId victim,
                                  TickIndex now, uint32_t window_ticks) const {
    Q16 total = 0;
    const TickIndex cutoff = now > window_ticks ? now - window_ticks : 0;

    for (uint32_t seq = OldestIndex(); seq < write_cursor_; ++seq) {
        const DamageRecord& record = At(seq);
        if (record.tick < cutoff) continue;
        if (record.victim != victim) continue;
        if (record.attacker != attacker) continue;
        total += record.amount;
    }

    return total;
}

Q16 CombatLedger::TotalDamageTo(EntityId victim, TickIndex now, uint32_t window_ticks) const {
    Q16 total = 0;
    const TickIndex cutoff = now > window_ticks ? now - window_ticks : 0;

    for (uint32_t seq = OldestIndex(); seq < write_cursor_; ++seq) {
        const DamageRecord& record = At(seq);
        if (record.tick < cutoff) continue;
        if (record.victim != victim) continue;
        total += record.amount;
    }

    return total;
}

EntityId CombatLedger::LastAttackerOf(EntityId victim, TickIndex now, uint32_t window_ticks) const {
    EntityId last = core::kInvalidEntity;
    const TickIndex cutoff = now > window_ticks ? now - window_ticks : 0;

    for (uint32_t seq = OldestIndex(); seq < write_cursor_; ++seq) {
        const DamageRecord& record = At(seq);
        if (record.tick < cutoff) continue;
        if (record.victim != victim) continue;
        last = record.attacker;
    }

    return last;
}

// ----------------------------------------------------------------- threat

// The AI threat table. Walks the window oldest first and keeps the eight
// biggest contributors in a fixed row array, because the threat model runs
// for every AI actor every few ticks and cannot afford to allocate.
//
// Rows are kept in first touch order and the linear scan below is cheap at
// eight entries. Anything past the eighth distinct attacker is folded into
// the residual, which the AI treats as ambient pressure.
uint32_t CombatLedger::CollectThreatRows(EntityId victim, TickIndex now,
                                         ThreatRow* rows, Q16* residual) const {
    uint32_t row_count = 0;
    *residual = 0;

    const TickIndex cutoff = now > kThreatWindowTicks ? now - kThreatWindowTicks : 0;

    for (uint32_t seq = OldestIndex(); seq < write_cursor_; ++seq) {
        const DamageRecord& record = At(seq);
        if (record.tick < cutoff) continue;
        if (record.victim != victim) continue;

        uint32_t found = kMaxThreatRows;
        for (uint32_t i = 0; i < row_count; ++i) {
            if (rows[i].attacker == record.attacker) {
                found = i;
                break;
            }
        }

        if (found != kMaxThreatRows) {
            rows[found].amount += record.amount;
            continue;
        }

        if (row_count < kMaxThreatRows) {
            rows[row_count].attacker = record.attacker;
            rows[row_count].amount = record.amount;
            ++row_count;
            continue;
        }

        *residual += record.amount;
    }

    return row_count;
}

// ------------------------------------------------------------------ death

// Called by the damage system the moment an actor's health crosses zero, on
// the tick it crosses, before anything else in the tick observes the death.
//
// Right now the whole bounty goes to whoever landed the last hit, which is
// why a player who softened a boss for twenty seconds gets nothing when a
// stray turret shot finishes it.
void CombatLedger::OnActorKilled(World& world, EntityId victim, EntityId killer) {
    const ActorDef& def = world.def_of(victim);
    const uint32_t bounty = def.bounty_gold;

    if (bounty == 0) return;

    const EntityId credited = killer != core::kInvalidEntity
                                  ? killer
                                  : LastAttackerOf(victim, world.tick, kBountyWindowTicks);

    if (credited == core::kInvalidEntity) return;

    EmitAward(world, credited, bounty, AwardReason::kKill);
}

// Award events are consumed at the end of the tick by the economy system,
// which pays them out and then hands them to the kill feed. The economy
// system takes them in the order they were queued.
void CombatLedger::EmitAward(World& world, EntityId recipient, uint32_t gold, AwardReason reason) {
    AwardEvent event;
    event.recipient = recipient;
    event.gold = gold;
    event.reason = reason;
    event.tick = world.tick;
    event.sequence = award_sequence_++;

    world.QueueAward(event);
}

// ------------------------------------------------------------- scoreboard

// End of match totals. Runs once, walks the whole ring, and writes into the
// scoreboard rows the match summary screen reads. Players are a dense array
// of at most sixteen slots so this one can index straight in.
void CombatLedger::FillScoreboard(const World& world, ScoreboardRow* rows, uint32_t row_count) const {
    for (uint32_t i = 0; i < row_count; ++i) {
        rows[i].damage_dealt = 0;
        rows[i].damage_taken = 0;
    }

    for (uint32_t seq = OldestIndex(); seq < write_cursor_; ++seq) {
        const DamageRecord& record = At(seq);

        const core::Slot dealer = world.player_slot_of(record.attacker);
        if (dealer != core::kInvalidSlot && dealer < row_count) {
            rows[dealer].damage_dealt += record.amount;
        }

        const core::Slot taker = world.player_slot_of(record.victim);
        if (taker != core::kInvalidSlot && taker < row_count) {
            rows[taker].damage_taken += record.amount;
        }
    }
}

// ------------------------------------------------------------------ debug

// Dumps the recent window to the tick log when the desync checker asks for
// it. Not called in shipping builds.
void CombatLedger::DumpWindow(TickIndex now, uint32_t window_ticks, LogSink& sink) const {
    const TickIndex cutoff = now > window_ticks ? now - window_ticks : 0;

    for (uint32_t seq = OldestIndex(); seq < write_cursor_; ++seq) {
        const DamageRecord& record = At(seq);
        if (record.tick < cutoff) continue;
        sink.Line("t=%u seq=%u %u -> %u amount=%d kind=%u",
                  record.tick, record.sequence, record.attacker, record.victim,
                  static_cast<int32_t>(record.amount), static_cast<uint32_t>(record.kind));
    }
}

}  // namespace sim
