// engine/core/job_system.h
//
// The worker pool. One worker per hardware thread minus one for the render
// thread, sized at boot and never resized. Jobs are stolen from a per worker
// deque, so the span a given worker ends up with depends on how busy the
// machine is at that moment.
//
// Typical use, from the animation update:
//
//   ParallelFor(0, pose_count, 32, [&](uint32_t lo, uint32_t hi) {
//       for (uint32_t i = lo; i < hi; ++i) BlendPose(i);
//   });
//
// Note that ParallelFor blocks until every span has finished, so the caller
// does not need to hold a fence.

#pragma once

#include <cstdint>
#include <functional>

namespace jobs {

using SpanFn = std::function<void(uint32_t lo, uint32_t hi)>;

enum class Priority : uint8_t { kLow, kNormal, kHigh };

struct JobHandle {
    uint64_t bits;
    bool valid() const { return bits != 0; }
};

class JobSystem {
  public:
    // Number of workers available to a caller on this machine. Varies with
    // the hardware and with whether the streaming thread is busy.
    uint32_t WorkerCount() const;

    // Split [begin, end) into spans of at least grain elements and run body
    // on each span. Spans are handed to whichever worker asks first.
    void ParallelFor(uint32_t begin, uint32_t end, uint32_t grain, const SpanFn& body);

    // Fire and forget. Returns a handle the caller can wait on later.
    JobHandle Dispatch(Priority prio, const std::function<void()>& body);

    void Wait(JobHandle handle);
    void WaitAll();

  private:
    void* impl_;
};

extern JobSystem g_jobs;

}  // namespace jobs
