// frame_budget.cpp
//
// Stage budget enforcement.
//
// The budgets are advisory in a shipping build and enforced in a development
// build. A stage that runs over its budget for a sustained run of frames
// raises a report rather than failing immediately, because a single frame
// over budget is usually a streaming hitch and not a regression.
//
// Copyright (c) Northlight Interactive. Internal performance source.

#include "eh/perf/frame_budget.h"

#include <cstdint>
#include <cstdio>

namespace nl {
namespace perf {

// Consecutive frames a stage has to stay over budget before it is reported.
inline constexpr std::int32_t kOverrunFramesBeforeReport = 30;

// A stage that exceeds its budget by more than this multiple is reported at
// once rather than waiting for the run of frames to build up.
inline constexpr float kImmediateReportMultiple = 4.0f;

struct StageTracker {
    std::int32_t consecutive_overruns = 0;
    float worst_ms = 0.0f;
    float total_ms = 0.0f;
    std::int64_t frames = 0;
};

StageTracker g_trackers[static_cast<std::size_t>(Stage::Count)];

void RecordStageTime(Stage stage, float measured_ms) {
    StageTracker& tracker = g_trackers[static_cast<std::size_t>(stage)];
    const float budget_ms = StageBudgetMs(stage);

    tracker.total_ms += measured_ms;
    ++tracker.frames;
    if (measured_ms > tracker.worst_ms) {
        tracker.worst_ms = measured_ms;
    }

    if (measured_ms <= budget_ms) {
        tracker.consecutive_overruns = 0;
        return;
    }

    ++tracker.consecutive_overruns;

    if (measured_ms > budget_ms * kImmediateReportMultiple) {
        ReportStageOverrun(stage, measured_ms, budget_ms, tracker.consecutive_overruns);
        tracker.consecutive_overruns = 0;
        return;
    }

    if (tracker.consecutive_overruns >= kOverrunFramesBeforeReport) {
        ReportStageOverrun(stage, measured_ms, budget_ms, tracker.consecutive_overruns);
        tracker.consecutive_overruns = 0;
    }
}

float StageAverageMs(Stage stage) {
    const StageTracker& tracker = g_trackers[static_cast<std::size_t>(stage)];
    if (tracker.frames == 0) {
        return 0.0f;
    }
    return tracker.total_ms / static_cast<float>(tracker.frames);
}

// Headroom left in a stage, as a fraction of its budget. Negative when the
// stage is over.
float StageHeadroomFraction(Stage stage, float measured_ms) {
    const float budget_ms = StageBudgetMs(stage);
    if (budget_ms <= 0.0f) {
        return 0.0f;
    }
    return (budget_ms - measured_ms) / budget_ms;
}

// ---------------------------------------------------------------- reporting

void DumpBudgetTable(std::FILE* out) {
    std::fprintf(out, "stage        weight   budget ms   average ms   worst ms\n");
    for (std::size_t i = 0; i < kStageCount; ++i) {
        const StageWeight& row = kStageWeights[i];
        const StageTracker& tracker = g_trackers[static_cast<std::size_t>(row.stage)];
        std::fprintf(out, "%-12s %6d %11.3f %12.3f %10.3f\n",
                     row.name, row.weight, StageBudgetMs(row.stage),
                     StageAverageMs(row.stage), tracker.worst_ms);
    }
}

void ResetBudgetTrackers() {
    for (std::size_t i = 0; i < static_cast<std::size_t>(Stage::Count); ++i) {
        g_trackers[i] = StageTracker{};
    }
}

}  // namespace perf
}  // namespace nl
