// queue_diagnostics.h
//
// Counters for the lock free queues.

#pragma once

#include <cstddef>
#include <cstdint>

namespace nl {
namespace memory {

class LogSink;

struct QueueCounters {
    std::uint64_t pushes = 0;
    std::uint64_t pops = 0;
    std::uint64_t contended_pushes = 0;
    std::uint64_t contended_pops = 0;
    std::uint64_t spin_total = 0;

    std::uint64_t window_pushes = 0;
    std::uint64_t window_pops = 0;
    std::uint64_t window_spins = 0;

    void RecordPush(std::uint32_t spins);
    void RecordPop(std::uint32_t spins);
    void BeginFrame(std::uint32_t frame_index);
    float ContentionRate() const;
};

std::ptrdiff_t PositionStrideBytes(const void* enqueue_member,
                                   const void* dequeue_member);

void DumpQueueDiagnostics(LogSink& log);

}  // namespace memory
}  // namespace nl
