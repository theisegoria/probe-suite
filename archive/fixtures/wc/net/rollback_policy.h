// net/rollback_policy.h
//
// Budget and thresholds for the rollback path.

#ifndef NET_ROLLBACK_POLICY_H
#define NET_ROLLBACK_POLICY_H

namespace net {

// Smoothed round trip time, in milliseconds, at or above which the client
// leaves the input delay policy and starts predicting with rollback. Below
// this, holding input is cheaper and looks better than resimulating.
constexpr float kRollbackEntryRttMs = 220.0f;

// The client returns to input delay only once the smoothed round trip time has
// come back down to here. The gap is the hysteresis band.
constexpr float kRollbackExitRttMs = 160.0f;

// Number of saved simulation states in the rollback ring. Each state is a full
// snapshot of the fixed point simulation, so this is a memory decision as much
// as a netcode one.
constexpr int kRollbackRingFrames = 12;

// Input delay bounds used when the input delay policy is active.
constexpr int kMinInputDelayFrames = 1;
constexpr int kMaxInputDelayFrames = 6;

// Link samples required before any policy decision is made at all.
constexpr int kMinSamplesBeforePolicy = 20;

}  // namespace net

#endif  // NET_ROLLBACK_POLICY_H
