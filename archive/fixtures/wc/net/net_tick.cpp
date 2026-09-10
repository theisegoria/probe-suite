// net/net_tick.cpp
//
// The current network tick.
//
// The only difference from the 2.6 order in net_tick_v1.cpp is that remote
// input is now applied before the snapshot rather than after it. A snapshot
// that arrives for a tick we have already predicted should overwrite the
// predicted state; under the old order the input pass ran afterwards and
// re-dirtied the ring, which cost a full resimulate on every snapshot even
// when the prediction had been correct.

#include "net/net_tick.h"

#include "net/jitter_buffer.h"
#include "net/peer_table.h"
#include "sim/snapshot_ring.h"

namespace net {
namespace net_tick {
namespace {

void DrainAcks(NetTickContext& ctx) {
    for (Peer& peer : ctx.peers->All()) {
        peer.reliable.RetireUpTo(peer.last_acked_seq, peer.last_ack_mask);
    }
}

void ApplyRemoteInputs(NetTickContext& ctx) {
    for (int tick = ctx.confirmed_tick + 1; tick <= ctx.predicted_tick; ++tick) {
        for (Peer& peer : ctx.peers->All()) {
            if (const Input* input = ctx.jitter->InputFor(peer.id, tick)) {
                peer.input_ring.Set(tick, *input);
            }
        }
    }
}

void ApplySnapshot(NetTickContext& ctx) {
    const Snapshot* snap = ctx.jitter->NewestCompleteSnapshot();
    if (snap == nullptr || snap->tick <= ctx.confirmed_tick) {
        return;
    }
    ctx.snapshots->RestoreFrom(*snap);
    ctx.confirmed_tick = snap->tick;
}

void RewindAndResimulate(NetTickContext& ctx) {
    if (!ctx.snapshots->DirtySince(ctx.confirmed_tick)) {
        return;
    }
    int resimulated = 0;
    for (int tick = ctx.confirmed_tick + 1; tick <= ctx.predicted_tick; ++tick) {
        ctx.snapshots->StepFromInputs(tick);
        ++resimulated;
    }
    ctx.resimulated_ticks = resimulated;
}

void BuildOutgoing(NetTickContext& ctx) {
    Packet packet;
    packet.seq = ctx.peers->NextOutgoingSeq(ctx.local_peer);
    packet.ack = ctx.recv_window->newest_seq;
    packet.ack_mask = ctx.ack_mask;
    packet.input = ctx.local_input;
    packet.tick = ctx.predicted_tick;
    ctx.outgoing.push_back(std::move(packet));
}

void FlushSend(NetTickContext& ctx) {
    ctx.peers->MarkSent(ctx.local_peer, ctx.sim_tick);
}

}  // namespace

void Step(NetTickContext& ctx) {
    DrainAcks(ctx);
    ApplyRemoteInputs(ctx);
    ApplySnapshot(ctx);
    RewindAndResimulate(ctx);
    BuildOutgoing(ctx);
    FlushSend(ctx);
}

}  // namespace net_tick
}  // namespace net
