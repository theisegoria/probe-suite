// render/ibl_prefilter.h
//
// Split sum image based lighting. The specular integral is factored into a
// prefiltered radiance term, sampled from a mip chain built per probe, and a
// scale and bias term read from the DFG lookup table.
//
// There are two prefiltered chains in the renderer. Reflection probes are
// captured at runtime into a fixed size cube array. The sky probe is built by
// the sky capture path from the atmosphere model and is rebuilt whenever the
// time of day moves far enough.

#ifndef RENDER_IBL_PREFILTER_H
#define RENDER_IBL_PREFILTER_H

#include "render/sky_probe_desc.h"

namespace render {

class CubeImage;

// Builds the prefiltered chain for a reflection probe capture.
void PrefilterReflectionProbe(const CubeImage& captured, CubeImage* out_chain);

// Mip to sample in a reflection probe's prefiltered chain for a given
// perceptual roughness. Implemented in ibl_prefilter.cpp.
float PrefilterMipForRoughness(float perceptual_roughness);

// The same question for a sky probe, whose chain is built and owned by the sky
// capture path. Implemented in sky_prefilter.cpp.
float PrefilterMipForRoughness(float perceptual_roughness, const SkyProbeDesc& sky);

}  // namespace render

#endif  // RENDER_IBL_PREFILTER_H
