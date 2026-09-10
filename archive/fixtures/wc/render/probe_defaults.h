// render/probe_defaults.h
//
// Defaults for the reflection probe capture path.
//
// A probe captures the world around a point into a small cube map, which the
// specular IBL lookup then samples. When a probe captures a surface that has
// no bound material, for example a proxy volume or a cutaway wall, the capture
// shader substitutes these values so that the captured radiance is stable
// rather than whatever the uninitialised constant buffer held.

#ifndef RENDER_PROBE_DEFAULTS_H
#define RENDER_PROBE_DEFAULTS_H

namespace render {

// Perceptual roughness substituted for surfaces captured without a material.
// Deliberately mid grey in roughness terms: a probe should not bake a mirror
// where there is no evidence of one.
constexpr float kProbeDefaultRoughness = 0.5f;

// Metallic substituted for the same case. Non metal is the safe assumption.
constexpr float kProbeDefaultMetallic = 0.0f;

// Reflectance substituted for the same case, matching common dielectrics.
constexpr float kProbeDefaultReflectance = 0.5f;

// Probes fade out over this distance, in metres, past their influence bounds,
// so that a moving camera does not pop between probes.
constexpr float kProbeBlendDistanceM = 1.5f;

// Captures are re rendered at most this often, in seconds, per probe.
constexpr float kProbeRecaptureIntervalS = 4.0f;

// Ambient occlusion applied to captured radiance before it is prefiltered.
constexpr float kProbeCaptureAoFloor = 0.35f;

}  // namespace render

#endif  // RENDER_PROBE_DEFAULTS_H
