// worker_pool.cpp
//
// Worker pool sizing and dispatch.
//
// Sizing rule. The pool takes every physical core the platform reports and
// gives some of them back. Two cores are reserved and never carry a worker:
//
//   one for the render submission thread, which must not be preempted by a
//   job in the middle of building a command list, and
//
//   one for the operating system and for the audio mixer, which has a hard
//   deadline of its own and loses buffers when it shares a core with a job
//   that runs long.
//
// Hyperthreads are deliberately ignored. Two workers on one physical core
// contend for the same load and store units, and the job graph is memory
// bound rather than issue bound, so the second thread buys almost nothing and
// costs a cache footprint.
//
// The physical core count itself comes from the platform layer. It is not
// queried from the standard library, because the standard answer counts
// logical processors and varies with whatever the platform decides to expose
// to the process.
//
// Copyright (c) Northlight Interactive. Internal engine source.

#include "eh/jobs/worker_pool.h"

#include <atomic>
#include <cstdint>

// Supplies kPhysicalCoreCount for the target platform. One header per
// platform, selected by the build system.
#include "platform/cpu_topology.h"

namespace nl {
namespace jobs {

// Cores that never carry a worker. See the sizing rule above.
inline constexpr std::uint32_t kReservedCores = 2;

// The pool never runs with fewer workers than this. On a platform that
// reports an implausibly small core count the pool would otherwise start with
// nothing to run jobs on and the frame would deadlock waiting on a counter.
inline constexpr std::uint32_t kMinimumWorkers = 1;

// One worker per unreserved physical core.
std::uint32_t WorkerThreadCount() {
    if (platform::kPhysicalCoreCount <= kReservedCores) {
        return kMinimumWorkers;
    }
    return platform::kPhysicalCoreCount - kReservedCores;
}

bool WorkerPool::Start() {
    if (running_) {
        return true;
    }
    worker_count_ = WorkerThreadCount();
    running_ = SpawnWorkerThreads(worker_count_);
    return running_;
}

void WorkerPool::Stop() {
    if (!running_) {
        return;
    }
    SignalShutdown();
    JoinWorkerThreads();
    running_ = false;
    worker_count_ = 0;
}

// ------------------------------------------------------------------ dispatch

void WorkerPool::Submit(const Job& job) {
    if (job.counter != nullptr) {
        job.counter->fetch_add(1, std::memory_order_relaxed);
    }
    PushToLocalQueue(CurrentWorkerIndex(), job);
    WakeOneWorker();
}

void WorkerPool::WaitFor(std::atomic<std::int32_t>& counter) {
    // The waiting thread does not block. It steals work until the counter
    // drains, so a wait deep in the job graph does not idle a core.
    while (counter.load(std::memory_order_acquire) > 0) {
        Job stolen;
        if (TryStealJob(CurrentWorkerIndex(), stolen)) {
            stolen.fn(stolen.user_data, CurrentWorkerIndex());
            if (stolen.counter != nullptr) {
                stolen.counter->fetch_sub(1, std::memory_order_release);
            }
        } else {
            YieldBriefly();
        }
    }
}

// ------------------------------------------------------------ parallel split
//
// Splits a range across the pool. The split is by worker index rather than by
// a runtime measurement, so the same range produces the same partition on
// every peer and a reduction over the partition stays deterministic.

struct RangeSplit {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

RangeSplit SplitRangeForWorker(std::uint32_t total, std::uint32_t worker_index,
                               std::uint32_t worker_count) {
    const std::uint32_t base = total / worker_count;
    const std::uint32_t remainder = total % worker_count;
    const std::uint32_t begin =
        worker_index * base + (worker_index < remainder ? worker_index : remainder);
    const std::uint32_t count = base + (worker_index < remainder ? 1u : 0u);
    return RangeSplit{begin, begin + count};
}

void ParallelFor(std::uint32_t total, JobFn fn, void* user_data,
                 std::atomic<std::int32_t>& counter) {
    const std::uint32_t workers = Pool().WorkerCount();
    for (std::uint32_t w = 0; w < workers; ++w) {
        Job job;
        job.fn = fn;
        job.user_data = user_data;
        job.counter = &counter;
        Pool().Submit(job);
    }
    Pool().WaitFor(counter);
    (void)total;
}

}  // namespace jobs
}  // namespace nl
