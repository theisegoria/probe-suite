// input_window.h
//
// Input window declarations.

#pragma once

#include <cstdint>

namespace nl {
namespace rollback {

struct InputFrame {
    std::uint16_t buttons = 0;
    std::int8_t   move_x = 0;
    std::int8_t   move_y = 0;
    std::int16_t  aim_yaw = 0;
    std::int16_t  aim_pitch = 0;
};

struct WindowStats {
    std::int64_t rollbacks = 0;
    std::int64_t deepest_rollback = 0;
    std::int64_t outside_window = 0;
    std::int64_t discarded_late = 0;
};

struct ConnectionReadout {
    std::int64_t absorbable_ticks = 0;
    float        absorbable_ms = 0.0f;
    std::int64_t input_delay_ticks = 0;
    std::int64_t max_rollback_ticks = 0;
    std::int64_t rollbacks = 0;
    std::int64_t deepest_rollback = 0;
    std::int64_t outside_window = 0;
};

class InputWindow {
public:
    bool CanAbsorb(std::int64_t input_tick, std::int64_t local_tick) const;
    void OnRemoteInput(std::int64_t input_tick, const InputFrame& frame,
                       std::int64_t local_tick);
    void OnLocalInput(std::int64_t sample_tick, const InputFrame& frame);
    bool ShouldStall(std::int64_t local_tick) const;
    void FillConnectionReadout(ConnectionReadout& out) const;

private:
    std::int64_t confirmed_tick_ = 0;
    WindowStats stats_;
};

}  // namespace rollback
}  // namespace nl
