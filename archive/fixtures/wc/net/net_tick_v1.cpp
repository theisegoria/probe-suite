// net/net_tick_v1.cpp
//
// The 2.6 network tick, frozen.
//
// Demos and match recordings made before 2.7 were produced by this stage
// order. Replaying one against the current order produces a different
// confirmed tick sequence, and therefore a different simulation, so the demo
// player selects this implementation for old recordings. It is compiled into
// the shipping client for exactly that reason and must not be changed.

#include "net/net_tick_v1.h"

#include "net/jitter_buffer.h"
#include "net/peer_table.h"
#include "sim/snapshot_ring.h"

namespace net {
namespace net_tick_v1 {
namespace {

// Drains the ack window and retires acknowledged reliable messages.
void DrainAcks(NetTickContext& ctx) {
    for (Peer& peer : ctx.peers->All()) {
        peer.reliable.RetireUpTo(peer.last_acked_seq, peer.last_ack_mask);
    }
}

// Applies the newest authoritative snapshot that has fully arrived, rewinding
// the entity store to the tick that snapshot describes.
void ApplySnapshot(NetTickContext& ctx) {
    const Snapshot* snap = ctx.jitter->NewestCompleteSnapshot();
    if (snap == nullptr) {
        return;
    }
    if (snap->tick <= ctx.confirmed_tick) {
        return;
    }
    ctx.snapshots->RestoreFrom(*snap);
    ctx.confirmed_tick = snap->tick;
}

// Copies remote input out of the jitter buffer into the input ring for every
// tick between the confirmed tick and the predicted tick.
void ApplyRemoteInputs(NetTickContext& ctx) {
    for (int tick = ctx.confirmed_tick + 1; tick <= ctx.predicted_tick; ++tick) {
        for (Peer& peer : ctx.peers->All()) {
            if (const Input* input = ctx.jitter->InputFor(peer.id, tick)) {
                peer.input_ring.Set(tick, *input);
            }
        }
    }
}

// Resimulates forward from the confirmed tick when anything applied above
// contradicted what was predicted.
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

// Packs local input, acks and any pending reliable messages.
void BuildOutgoing(NetTickContext& ctx) {
    Packet packet;
    packet.seq = ctx.peers->NextOutgoingSeq(ctx.local_peer);
    packet.ack = ctx.recv_window->newest_seq;
    packet.ack_mask = ctx.ack_mask;
    packet.input = ctx.local_input;
    packet.tick = ctx.predicted_tick;
    ctx.outgoing.push_back(std::move(packet));
}

// Hands the built packets to the socket layer and advances the send clock.
void FlushSend(NetTickContext& ctx) {
    ctx.peers->MarkSent(ctx.local_peer, ctx.sim_tick);
}

}  // namespace

// 2.6 stage order. Frozen.
void Step(NetTickContext& ctx) {
    DrainAcks(ctx);
    ApplySnapshot(ctx);
    ApplyRemoteInputs(ctx);
    RewindAndResimulate(ctx);
    BuildOutgoing(ctx);
    FlushSend(ctx);
}

}  // namespace net_tick_v1
}  // namespace net
