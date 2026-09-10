// net/session.cpp
//
// Owns everything that lives for the length of a match: the peer table, the
// jitter buffer, the ack window, the clock estimate, and the ring of saved
// simulation states used for rollback.
//
// Session does not decide what happens inside a network tick. It fills the
// NetTickContext, hands it to net_tick::Step, and then reads back what the
// step decided. The stage order inside a tick lives in net_tick.cpp, which is
// the only place that order is written down.

#include "net/session.h"

#include "net/jitter_buffer.h"
#include "net/net_tick.h"
#include "net/peer_table.h"
#include "sim/sim_rates.h"
#include "sim/snapshot_ring.h"

namespace net {
namespace {

// Sequence numbers are 16 bit and wrap. Everything that compares them has to
// go through this rather than comparing the raw values.
bool SeqNewerThan(uint16_t a, uint16_t b) {
    return (static_cast<uint16_t>(a - b) < 0x8000u);
}

// The ack field is a 32 bit bitmask of the packets before the acked sequence,
// which is enough history at the send rates we use.
uint32_t BuildAckMask(const RecvWindow& window) {
    uint32_t mask = 0;
    for (int i = 0; i < 32; ++i) {
        if (window.Received(static_cast<uint16_t>(window.newest_seq - i - 1))) {
            mask |= (1u << i);
        }
    }
    return mask;
}

}  // namespace

void Session::BeginMatch(const MatchParams& params) {
    peers_.Reset(params.peer_count);
    jitter_.Reset(params.jitter_buffer_ticks);
    snapshots_.Reset(params.rollback_ring_frames);
    local_peer_ = params.local_peer;
    confirmed_tick_ = 0;
    predicted_tick_ = 0;
}

void Session::PumpSocket() {
    Packet packet;
    while (socket_.Receive(&packet)) {
        if (!peers_.Validate(packet.peer, packet.token)) {
            continue;
        }
        if (SeqNewerThan(packet.seq, recv_window_.newest_seq)) {
            recv_window_.newest_seq = packet.seq;
        }
        recv_window_.MarkReceived(packet.seq);
        jitter_.Push(std::move(packet));
    }
}

// One network tick. Everything the step needs is gathered here; everything the
// step decides is read back out of the context afterwards.
void Session::TickNetwork(int sim_tick, float dt_seconds) {
    NetTickContext ctx;
    ctx.sim_tick = sim_tick;
    ctx.dt_seconds = dt_seconds;
    ctx.local_peer = local_peer_;
    ctx.peers = &peers_;
    ctx.jitter = &jitter_;
    ctx.snapshots = &snapshots_;
    ctx.recv_window = &recv_window_;
    ctx.ack_mask = BuildAckMask(recv_window_);
    ctx.confirmed_tick = confirmed_tick_;
    ctx.predicted_tick = predicted_tick_;
    ctx.local_input = local_input_;

    net_tick::Step(ctx);

    confirmed_tick_ = ctx.confirmed_tick;
    predicted_tick_ = ctx.predicted_tick;
    resimulated_ticks_ += ctx.resimulated_ticks;
    if (ctx.outgoing.size() > 0) {
        socket_.SendAll(ctx.outgoing);
    }
}

// Demo playback of recordings made before 2.7 goes through the frozen stage
// order in net_tick_v1.cpp instead, because the recording only makes sense
// against the order that produced it.
void Session::TickDemoPlayback(int sim_tick, float dt_seconds) {
    NetTickContext ctx;
    ctx.sim_tick = sim_tick;
    ctx.dt_seconds = dt_seconds;
    ctx.local_peer = local_peer_;
    ctx.peers = &peers_;
    ctx.jitter = &jitter_;
    ctx.snapshots = &snapshots_;
    ctx.recv_window = &recv_window_;
    ctx.confirmed_tick = confirmed_tick_;
    ctx.predicted_tick = predicted_tick_;

    net_tick_v1::Step(ctx);

    confirmed_tick_ = ctx.confirmed_tick;
    predicted_tick_ = ctx.predicted_tick;
}

float Session::EstimatedClockOffsetMs() const {
    return clock_.offset_ms;
}

}  // namespace net
