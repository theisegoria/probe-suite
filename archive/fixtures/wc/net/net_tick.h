// net/net_tick.h
//
// One network tick. The stage order is defined in net_tick.cpp.

#ifndef NET_NET_TICK_H
#define NET_NET_TICK_H

#include "net/net_types.h"

namespace net {
namespace net_tick {

// Runs every stage of the current network tick in the current order. Session
// owns the buffers; this owns the order.
void Step(NetTickContext& ctx);

}  // namespace net_tick
}  // namespace net

#endif  // NET_NET_TICK_H
