// engine/render/shaders/status_burn.hlsl
//
// The burn overlay. Draws the ember crust that creeps over an actor while it
// is on fire, driven by the stack fraction the HUD hands us.
//
// Presentation only. Nothing this shader computes is ever read back.

cbuffer BurnConstants : register(b0)
{
    float4x4 gWorldViewProj;
    float3   gEmberTint;
    float    gStackFraction;      // 0 to 1, from StatusSystem::StackFractionForHud
    float    gUiTimeSeconds;      // wall clock, for the flicker
    float    gInterpAlphaValue;   // where this frame sits between snapshots
    float2   gNoiseScale;
};

Texture2D    gCrustNoise  : register(t0);
Texture2D    gEmberRamp   : register(t1);
SamplerState gLinearWrap  : register(s0);

struct VSOut
{
    float4 clip_position : SV_POSITION;
    float3 world_normal  : NORMAL;
    float2 uv            : TEXCOORD0;
    float  ember_seed    : TEXCOORD1;
};

VSOut VSMain(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0)
{
    VSOut o;
    o.clip_position = mul(float4(position, 1.0), gWorldViewProj);
    o.world_normal  = normal;
    o.uv            = uv * gNoiseScale;
    o.ember_seed    = frac(dot(position, float3(12.9898, 78.233, 37.719)));
    return o;
}

// The crust threshold walks up the noise field as the stack fraction climbs,
// with a gamma on it so the first few stacks read strongly and the last ten
// barely move the picture.
float CrustCoverage(float2 uv, float fraction)
{
    float noise = gCrustNoise.Sample(gLinearWrap, uv).r;
    float shaped = pow(saturate(fraction), 0.45);
    return smoothstep(shaped - 0.12, shaped + 0.12, 1.0 - noise);
}

// Embers pulse at a couple of hertz with a per vertex phase offset so the
// whole crust does not breathe in unison.
float EmberPulse(float seed)
{
    float phase = gUiTimeSeconds * 6.2831853 * 1.7 + seed * 6.2831853;
    return 0.65 + 0.35 * sin(phase);
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float coverage = CrustCoverage(input.uv, gStackFraction);
    float pulse    = EmberPulse(input.ember_seed);

    float heat = saturate(coverage * pulse);
    float3 ember = gEmberRamp.Sample(gLinearWrap, float2(heat, 0.5)).rgb * gEmberTint;

    // Falls off towards grazing angles so the silhouette does not glow.
    float facing = saturate(dot(normalize(input.world_normal), float3(0.0, 0.0, 1.0)));
    float rim = pow(1.0 - facing, 2.0);

    float3 colour = lerp(ember, ember * 0.35, rim);
    float alpha = coverage * saturate(gStackFraction * 4.0);

    return float4(colour, alpha);
}
