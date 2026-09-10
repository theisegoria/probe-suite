// rollback_config.cpp
//
// Loads the rollback configuration and sizes the snapshot ring from it.
//
// The rollback window is not a compile time constant. It was one until the
// second network test, at which point it became clear that the right value
// differs by region and by platform, and that shipping a patch to change it
// was not workable. It now lives in the network configuration file that ships
// alongside the executable, and every buffer that depends on it is sized at
// boot from whatever that file says.
//
// That means this file cannot tell you what the window is. It can tell you
// where the window comes from, what happens if the file is missing, and what
// the engine does with the value once it has it.
//
// Copyright (c) Northlight Interactive. Internal net source.

#include "eh/net/rollback_config.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "eh/core/ini.h"

namespace nl {
namespace net {

// Where the shipped configuration lives, relative to the content root. Cooked
// into the package by the build; not present in a source checkout.
inline constexpr const char* kNetConfigPath = "config/net.ini";

// Keys read out of it.
inline constexpr const char* kKeyMaxRollbackTicks = "net.rollback.max_ticks";
inline constexpr const char* kKeyInputDelayTicks  = "net.rollback.input_delay_ticks";
inline constexpr const char* kKeyHardDesync       = "net.rollback.hard_desync_on_overrun";

// Sanity bounds. These are not the shipping values and are not tuned: they
// exist so that a corrupt or hand edited file cannot make the engine allocate
// a snapshot ring large enough to exhaust memory, or small enough that the
// rollback path divides by zero. A value inside these bounds is accepted as
// authored, whatever it is.
inline constexpr std::int32_t kSaneMinTicks = 1;
inline constexpr std::int32_t kSaneMaxTicks = 240;

bool RollbackSystem::Initialise() {
    core::IniFile ini;
    if (!ini.Load(kNetConfigPath)) {
        // No configuration, no netcode. The engine refuses to start an online
        // session rather than inventing a window, because two peers that
        // invent different windows disagree about when a desync happened.
        std::fprintf(stderr, "net: %s missing, online play disabled\n", kNetConfigPath);
        return false;
    }

    std::int32_t max_ticks = 0;
    std::int32_t delay_ticks = 0;
    if (!ini.GetInt(kKeyMaxRollbackTicks, max_ticks) ||
        !ini.GetInt(kKeyInputDelayTicks, delay_ticks)) {
        std::fprintf(stderr, "net: %s is missing a required key\n", kNetConfigPath);
        return false;
    }

    if (max_ticks < kSaneMinTicks || max_ticks > kSaneMaxTicks) {
        std::fprintf(stderr, "net: %s out of sane bounds, refusing\n", kKeyMaxRollbackTicks);
        return false;
    }

    config_.max_rollback_ticks = max_ticks;
    config_.input_delay_ticks = delay_ticks;
    ini.GetBool(kKeyHardDesync, config_.hard_desync_on_overrun);

    // The snapshot ring holds one entry per rollback tick plus one for the
    // confirmed state itself. Sized here and never resized, so a rollback
    // never allocates.
    snapshots_.resize(static_cast<std::size_t>(config_.max_rollback_ticks) + 1);
    return true;
}

std::int32_t RollbackSystem::AbsorbableTicks() const {
    return config_.max_rollback_ticks + config_.input_delay_ticks;
}

bool RollbackSystem::RollbackTo(std::int64_t tick) {
    const std::int64_t distance = confirmed_tick_ - tick;
    if (distance < 0) {
        return false;
    }
    if (distance > config_.max_rollback_ticks) {
        // Beyond the window. Either desync loudly or wait for a full snapshot,
        // depending on how the configuration asked us to fail.
        if (config_.hard_desync_on_overrun) {
            ReportDesync(tick, confirmed_tick_);
            return false;
        }
        RequestFullSnapshot();
        return false;
    }

    const std::size_t slot =
        static_cast<std::size_t>(tick % static_cast<std::int64_t>(snapshots_.size()));
    RestoreSnapshot(snapshots_[slot]);
    return true;
}

// ------------------------------------------------------------- diagnostics

// The netcode overlay prints the configured window so that a tester looking
// at a bad connection can tell a window that is too small from a connection
// that is simply worse than the window can cover.
void RollbackSystem::DescribeConfiguration(char* out, std::size_t out_bytes) const {
    std::snprintf(out, out_bytes,
                  "rollback window %d ticks, input delay %d ticks, absorbable %d ticks",
                  config_.max_rollback_ticks, config_.input_delay_ticks,
                  AbsorbableTicks());
}

}  // namespace net
}  // namespace nl
