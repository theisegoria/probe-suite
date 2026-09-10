// net/relay_policy.h
//
// Thresholds for the relay path. When a direct peer to peer connection cannot
// be established, or degrades badly enough, traffic is moved to a regional
// relay. The relay adds a hop, so it is only a win when the direct path is
// genuinely poor.
//
// Nothing here participates in the simulation. Relay selection is negotiated
// out of band by the session layer before the match starts and can be redone
// mid match without touching simulation state.

#ifndef NET_RELAY_POLICY_H
#define NET_RELAY_POLICY_H

namespace net {

// Round trip time, in milliseconds, above which a direct connection is
// considered failing and the session tries to fail over to a relay. Measured
// on the same smoothed round trip estimate the rest of the netcode uses.
constexpr float kRelayFailoverRttMs = 180.0f;

// Round trip time below which the session will try to drop back off the relay
// onto a direct connection. The gap between the two is intentional.
constexpr float kRelayReturnRttMs = 120.0f;

// A relay hop is only worth taking if it is at least this much better than the
// direct path, in milliseconds, otherwise the extra hop is pure loss.
constexpr float kRelayMinImprovementMs = 25.0f;

// Consecutive failed probes before a relay candidate is struck off for the
// rest of the session.
constexpr int kRelayProbeFailureLimit = 3;

// Probe cadence in milliseconds while sitting on a relay.
constexpr int kRelayProbeIntervalMs = 4000;

// Packet loss fraction above which the relay is preferred regardless of round
// trip time, because loss hurts a rollback client far more than latency does.
constexpr float kRelayLossFailoverFraction = 0.08f;

}  // namespace net

#endif  // NET_RELAY_POLICY_H
