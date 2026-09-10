// rollback_config.h
//
// Runtime configuration for the rollback netcode.
//
// Copyright (c) Northlight Interactive. Internal net header.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nl {
namespace net {

struct SimulationSnapshot;

struct RollbackConfig {
    // Ticks of rollback the peer is willing to perform. Read from the shipped
    // network configuration file at boot. Nothing in the engine picks this
    // number: it is a tuning value that lives with the rest of the network
    // configuration so it can be changed without a patch to the executable.
    std::int32_t max_rollback_ticks = 0;

    // Ticks of local input delay. Also configured, and traded against the
    // rollback window: more delay means fewer rollbacks and a less responsive
    // control feel.
    std::int32_t input_delay_ticks = 0;

    // Whether a peer that exceeds the rollback window desyncs loudly or tries
    // to resynchronise from the next full snapshot.
    bool hard_desync_on_overrun = true;
};

class RollbackSystem {
public:
    // Loads the configuration, then sizes every buffer from it.
    bool Initialise();

    std::int32_t MaxRollbackTicks() const { return config_.max_rollback_ticks; }
    std::int32_t InputDelayTicks() const { return config_.input_delay_ticks; }

    // Ticks the peer can absorb before it has to stall: the rollback window
    // plus the input delay.
    std::int32_t AbsorbableTicks() const;

    bool RollbackTo(std::int64_t tick);

    // Formats the loaded configuration for the netcode overlay.
    void DescribeConfiguration(char* out, std::size_t out_bytes) const;

private:
    RollbackConfig config_;
    std::vector<SimulationSnapshot> snapshots_;
    std::int64_t confirmed_tick_ = 0;
};

}  // namespace net
}  // namespace nl
