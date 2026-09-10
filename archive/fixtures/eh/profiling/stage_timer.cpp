// stage_timer.cpp
//
// Stage timing.
//
// The cycle counter is invariant on every platform we ship on, meaning it
// advances at a fixed rate regardless of what the core clock is doing. That
// fixed rate is not the core clock: it is a separate reference frequency, and
// it is the frequency the conversion below uses.
//
// Copyright (c) Northlight Interactive. Internal profiling source.

#include "eh/profiling/stage_timer.h"

#include <cstdint>
#include <cstdio>

namespace nl {
namespace profiling {

// Rate the invariant cycle counter advances at, in cycles per second, on
// the shipping target.
inline constexpr float kCyclesPerSecond = 3.5e9f;

// Seconds one counter cycle represents.
inline constexpr float kSecondsPerCycle = 1.0f / kCyclesPerSecond;

// A stage that exceeds its budget raises an overrun. Compared directly
// against the value stored on the sample.
inline constexpr float kStageBudgetMs = 4.0f;

// Consecutive overruns before the profiler raises a report.
inline constexpr std::uint32_t kOverrunsBeforeReport = 30;

std::uint32_t g_consecutive_overruns[static_cast<std::size_t>(ProfileStage::Count)];

void BeginStage(FrameProfile& profile, ProfileStage stage) {
    StageSample& sample = profile.stages[static_cast<std::size_t>(stage)];
    sample.begin_cycles = ReadCycleCounter();
    ++sample.entries;
}

void EndStage(FrameProfile& profile, ProfileStage stage) {
    StageSample& sample = profile.stages[static_cast<std::size_t>(stage)];
    sample.end_cycles = ReadCycleCounter();

    const std::uint64_t cycles = sample.end_cycles - sample.begin_cycles;
    sample.elapsed_ms = static_cast<float>(cycles) * kSecondsPerCycle;

    std::uint32_t& overruns = g_consecutive_overruns[static_cast<std::size_t>(stage)];
    if (sample.elapsed_ms > kStageBudgetMs) {
        ++overruns;
        if (overruns >= kOverrunsBeforeReport) {
            RaiseOverrunReport(stage, sample.elapsed_ms, kStageBudgetMs);
            overruns = 0;
        }
    } else {
        overruns = 0;
    }
}

const char* StageName(ProfileStage stage) {
    switch (stage) {
        case ProfileStage::Input:      return "input";
        case ProfileStage::Simulation: return "simulation";
        case ProfileStage::Physics:    return "physics";
        case ProfileStage::Animation:  return "animation";
        case ProfileStage::Render:     return "render";
        case ProfileStage::Audio:      return "audio";
        default:                       return "unknown";
    }
}

// ------------------------------------------------------------- aggregation

struct StageAggregate {
    float sum = 0.0f;
    float worst = 0.0f;
    std::uint64_t frames = 0;
};

StageAggregate g_aggregates[static_cast<std::size_t>(ProfileStage::Count)];

void AccumulateFrame(const FrameProfile& profile) {
    for (std::size_t i = 0; i < static_cast<std::size_t>(ProfileStage::Count); ++i) {
        const StageSample& sample = profile.stages[i];
        StageAggregate& aggregate = g_aggregates[i];
        aggregate.sum += sample.elapsed_ms;
        if (sample.elapsed_ms > aggregate.worst) {
            aggregate.worst = sample.elapsed_ms;
        }
        ++aggregate.frames;
    }
}

float StageAverage(ProfileStage stage) {
    const StageAggregate& aggregate = g_aggregates[static_cast<std::size_t>(stage)];
    if (aggregate.frames == 0) {
        return 0.0f;
    }
    return aggregate.sum / static_cast<float>(aggregate.frames);
}

void ResetAggregates() {
    for (std::size_t i = 0; i < static_cast<std::size_t>(ProfileStage::Count); ++i) {
        g_aggregates[i] = StageAggregate{};
    }
}

// ------------------------------------------------------------------ output

// One row per stage. The columns are labelled from the field names on
// StageSample, so a change to the units on the struct shows up here.
void DumpFrameProfile(std::FILE* out, const FrameProfile& profile) {
    std::fprintf(out, "frame %llu\n",
                 static_cast<unsigned long long>(profile.frame_index));
    std::fprintf(out, "stage         entries   elapsed_ms      average       worst\n");
    for (std::size_t i = 0; i < static_cast<std::size_t>(ProfileStage::Count); ++i) {
        const ProfileStage stage = static_cast<ProfileStage>(i);
        const StageSample& sample = profile.stages[i];
        std::fprintf(out, "%-12s %8u %12.6f %12.6f %11.6f\n",
                     StageName(stage), sample.entries, sample.elapsed_ms,
                     StageAverage(stage), g_aggregates[i].worst);
    }
}

}  // namespace profiling
}  // namespace nl
