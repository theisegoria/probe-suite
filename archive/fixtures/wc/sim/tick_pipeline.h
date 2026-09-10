// sim/tick_pipeline.h
//
// Tick pipelines.
//
// A pipeline is an order over systems. The engine ships more than one, because
// the editor preview and the shipping simulation do not want the same thing
// out of a tick, but only one of them is authoritative.
//
//   tick_pipeline_authoritative.cpp  the order every peer in a match runs
//   tick_pipeline_editor.cpp         the editor preview order
//
// The authoritative order is part of the simulation contract. Two peers
// running different authoritative orders will desync on the first tick where
// any system reads state another system wrote, which in practice is the first
// tick with a contact in it.

#ifndef SIM_TICK_PIPELINE_H
#define SIM_TICK_PIPELINE_H

#include "sim/systems.h"

namespace sim {

class World;
struct CommandBuffer;

// The order run by the shipping client and the dedicated server. Defined in
// tick_pipeline_authoritative.cpp.
void RunAuthoritativeTick(World& world, const CommandBuffer& commands);

// The order run by the editor viewport when previewing a level. Defined in
// tick_pipeline_editor.cpp.
void RunEditorPreviewTick(World& world, const CommandBuffer& commands);

// Profiling hook. Called once per system per tick by whichever pipeline is
// running, with the id from systems.h.
void OnSystemBegin(SystemId id);
void OnSystemEnd(SystemId id);

}  // namespace sim

#endif  // SIM_TICK_PIPELINE_H
