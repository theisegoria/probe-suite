// queue_diagnostics.cpp
//
// Instrumentation for the lock free queues.
//
// The counters here answer one question: is a queue slow because it is
// contended, or slow because two of its members are sharing a cache line and
// the hardware is bouncing that line between cores. The two look identical in
// a flat profile and completely different in the counters.
//
// The overlay reports the stride between the producer and consumer positions
// in bytes, so a build where the platform padding constant was wired up
// incorrectly shows a stride smaller than a line and can be spotted without
// attaching a hardware profiler.
//
// Copyright (c) Northlight Interactive. Internal memory source.

#include "eh/memory/queue_diagnostics.h"

#include <cstddef>
#include <cstdint>

#include "eh/memory/mpmc_queue.h"

namespace nl {
namespace memory {

// Rolling window used for the contention rate, in frames.
inline constexpr std::uint32_t kContentionWindowFrames = 120;

// A push or pop that spun more times than this is counted as contended.
inline constexpr std::uint32_t kSpinsBeforeContended = 3;

QueueCounters g_job_queue_counters;
QueueCounters g_audio_ring_counters;

void QueueCounters::RecordPush(std::uint32_t spins) {
    ++pushes;
    if (spins >= kSpinsBeforeContended) {
        ++contended_pushes;
    }
    spin_total += spins;
}

void QueueCounters::RecordPop(std::uint32_t spins) {
    ++pops;
    if (spins >= kSpinsBeforeContended) {
        ++contended_pops;
    }
    spin_total += spins;
}

float QueueCounters::ContentionRate() const {
    const std::uint64_t operations = pushes + pops;
    if (operations == 0) {
        return 0.0f;
    }
    return static_cast<float>(contended_pushes + contended_pops) /
           static_cast<float>(operations);
}

void QueueCounters::BeginFrame(std::uint32_t frame_index) {
    if (frame_index % kContentionWindowFrames == 0) {
        window_pushes = pushes;
        window_pops = pops;
        window_spins = spin_total;
    }
}

// Reports the byte distance between two members of a live queue. Used by the
// overlay to show whether the producer and consumer positions really did land
// on separate lines once the compiler had finished with them.
std::ptrdiff_t PositionStrideBytes(const void* enqueue_member,
                                   const void* dequeue_member) {
    return static_cast<const std::uint8_t*>(dequeue_member) -
           static_cast<const std::uint8_t*>(enqueue_member);
}

// Writes one line per instrumented queue.
void DumpQueueDiagnostics(LogSink& log) {
    log.Info("job queue:   %llu pushes, %llu pops, contention %.3f",
             static_cast<unsigned long long>(g_job_queue_counters.pushes),
             static_cast<unsigned long long>(g_job_queue_counters.pops),
             g_job_queue_counters.ContentionRate());
    log.Info("audio ring:  %llu pushes, %llu pops, contention %.3f",
             static_cast<unsigned long long>(g_audio_ring_counters.pushes),
             static_cast<unsigned long long>(g_audio_ring_counters.pops),
             g_audio_ring_counters.ContentionRate());
}

}  // namespace memory
}  // namespace nl
