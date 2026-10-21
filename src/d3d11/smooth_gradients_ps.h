// The replacement tone pixel shaders for the game's five tonemapping passes (docs/bloom-artifact-2026-10-20.md).
//
// The game writes its tonemapped image to an 8-bit UNORM target with no dither, so a dim glow steps from one code value to
// the next and reads as bands. Each shader below is one of the game's tone passes with a triangular +-1 LSB dither added
// just before the final write (edvrSmoothGradients). The dither fades in over the first LSB, so black stays black, and
// alpha is never touched.
//
// Each shader is granted to one exact game hash:
//   E8948389387DA083  stock tone, bloom on           (kSmoothGradientsE894PS)
//   9270C355389DA302  tone and bloom composite       (kSmoothGradients9270PS)
//   FEE777E92850B390  stock tone, bloom off         (kSmoothGradientsFEE7PS)
//   DE65BFFF2F12ECC6  tone and depth-of-field       (kSmoothGradientsDE65PS)
//   6E83D02E7422C5BA  the 3D menu's tone            (kSmoothGradients6E83PS)
//
// THESE ARE TRANSCRIPTIONS, NOT REWRITES. Each text was written from the disassembly of ITS OWN shader, instruction by
// instruction and in order: every mad is mad(), every branch the original kept is [branch], and the operands are the
// original's. The input and output signatures (TEXCOORD1 at register 0, SV_Target0), the constant buffer size (cb2[N]) and
// every sampler and texture slot and type are the original's. tools/smooth_gradients_test runs the original bytecode and
// each transcription on the same inputs, and checks the dither against the unrounded result.
#pragma once

namespace edvr {

// The dither, shared by all five, as one string. EDVR_TONE_DITHER 0 leaves the value as the game computed it, which is how the
// equivalence test compares a transcription with its original. It is written as ordinary string pieces rather than one raw
// string: MSVC does not read a raw string that spans lines inside a #define when a line of it starts with '#', and the HLSL
// directives below must start their lines.
#define EDVR_SMOOTH_GRADIENTS_COMMON_HLSL \
    "#ifndef EDVR_TONE_DITHER\n" \
    "#define EDVR_TONE_DITHER 1\n" \
    "#endif\n" \
    "uint sgPcg(uint v){ uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }\n" \
    "float3 edvrSmoothGradients(float3 c, float2 pixel) {\n" \
    "#if EDVR_TONE_DITHER\n" \
    "    uint2 p = uint2(pixel);\n" \
    "    uint h1 = sgPcg(p.x ^ sgPcg(p.y));\n" \
    "    uint h2 = sgPcg(h1);\n" \
    "    float n = (float(h1 & 0xFFFFu) + float(h2 & 0xFFFFu)) / 65535.0 - 1.0;   // triangular, -1..1\n" \
    "    return c + n * saturate(c * 255.0) / 255.0;                               // fades in over the first LSB: black stays black\n" \
    "#else\n" \
    "    return c;\n" \
    "#endif\n" \
    "}\n"

// Stock tone, bloom on: the scene at t1, the bloom composite at t2, the 3D grade at t0.
constexpr char kSmoothGradientsE894PS[] = EDVR_SMOOTH_GRADIENTS_COMMON_HLSL R"HLSL(
cbuffer CB2 : register(b2) { float4 cb2[16]; };
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
Texture3D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);

float4 main(float3 v0 : TEXCOORD1, float4 pos : SV_Position) : SV_Target {
    float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, o = 0.0;
    r0 = t1.Sample(s1, v0.xy);
    r0 = r0 * cb2[2].x;
    r1.x = cb2[2].x * 64500.0;
    r0 = max(r0, 0.0);
    r0 = min(r1.xxxx, r0);
    r1.yzw = t2.Sample(s2, v0.xy).xyz;
    r1.yzw = r1.yzw * cb2[2].x;
    r1.yzw = max(r1.yzw, 0.0);
    r1.yzw = min(r1.xxx, r1.yzw);
    [branch] if (asuint(cb2[8].z)) { r2.x = 1.39999998; } else { r2.x = 0.0; }
    r2.x = r2.x + cb2[8].x;
    r2.y = v0.z * 8.0;
    r2.y = log2(r2.y);
    r2.x = -r2.x + r2.y;
    r2.x = r2.x + -3.0;
    r2.z = cb2[7].w + cb2[7].w;
    r2.x = r2.x * cb2[8].y;
    r2.x = exp2(r2.x);
    r2.x = r2.x + 1.0;
    r2.x = log2(r2.x);
    r2.x = mad(r2.x, 0.301030010, r2.z);
    r2.x = r2.z / r2.x;
    r2.x = log2(r2.x);
    r2.x = r2.x * cb2[7].w;
    r2.x = exp2(r2.x);
    r2.x = mad(cb2[7].z, 1.02999997, -r2.x);
    r2.x = r2.x / v0.z;
    r2.x = log2(r2.x);
    r2.x = r2.x - cb2[7].y;
    r2.x = exp2(r2.x);
    r3.xyz = r0.xyz * r2.x;
    [branch] if (asuint(cb2[8].z)) { r2.x = 1.39999998; } else { r2.x = 0.0; }
    r2.x = r2.x + cb2[8].x;
    r2.x = -r2.x + r2.y;
    r2.x = r2.x + -3.0;
    r2.x = r2.x * cb2[8].y;
    r2.x = exp2(r2.x);
    r2.x = r2.x + 1.0;
    r2.x = log2(r2.x);
    r2.x = mad(r2.x, 0.301030010, r2.z);
    r2.x = r2.z / r2.x;
    r2.x = log2(r2.x);
    r2.x = r2.x * cb2[7].w;
    r2.x = exp2(r2.x);
    r2.x = mad(cb2[7].z, 1.02999997, -r2.x);
    r2.x = r2.x / v0.z;
    r2.x = log2(r2.x);
    r2.x = r2.x - cb2[7].y;
    r2.x = exp2(r2.x);
    [branch] if (cb2[1].w >= 0.0) {
        r2.yzw = mad(r2.x, r1.yzw, -r3.xyz);
        r2.yzw = mad(cb2[1].w, r2.yzw, r3.xyz);
    } else {
        r2.yzw = mad(r2.x, r1.yzw, r3.xyz);
    }
    r1.yzw = max(r2.yzw, 0.0);
    r1.xyz = min(r1.xxx, r1.yzw);
    r1.xyz = r1.xyz * cb2[6].y;
    r2.xyz = mad(r1.xyz, 8.46800041, 1.0);
    r2.xyz = mad(r1.xyz, r2.xyz, -0.00295699993);
    r2.xyz = mad(r1.xyz, r2.xyz, 1.00400001e-04);
    r2.xyz = mad(r1.xyz, r2.xyz, -1.27400000e-07);
    r3.xyz = mad(r1.xyz, 8.36040020, 1.82270002);
    r3.xyz = mad(r1.xyz, r3.xyz, 2.18899995e-01);
    r3.xyz = mad(r1.xyz, r3.xyz, -2.11700005e-03);
    r1.xyz = mad(r1.xyz, r3.xyz, 3.67300017e-05);
    r1.xyz = saturate(r2.xyz / r1.xyz);
    r1.xyz = log2(r1.xyz);
    r1.xyz = r1.xyz * cb2[9].z;
    r1.xyz = r1.xyz * 2.20000005;
    r1.xyz = exp2(r1.xyz);
    r1.w = 1.0 - cb2[0].x;
    r2.x = cb2[0].x * 0.5;
    r2.xyz = mad(r1.xyz, r1.w, r2.x);
    r2.xyz = t0.Sample(s0, r2.xyz).xyz;
    r2.xyz = r2.xyz - r1.xyz;
    r1.xyz = mad(cb2[11].x, r2.xyz, r1.xyz);
    r1.xyz = saturate(mad(r1.xyz, cb2[10].y, cb2[9].y));
    r1.w = dot(r1.xyz, float3(0.308600008, 0.609399974, 0.0820000023));
    r1.xyz = r1.xyz - r1.w;
    r1.xyz = mad(cb2[10].z, r1.xyz, r1.w);
    r0.xyz = r1.xyz * cb2[10].x;
    r0 = max(r0, 0.0);
    r0 = min(r0, 64500.0);
    [branch] if (asuint(cb2[15].y)) { r0.xyz = 0.0; }
    o = r0;
    o.rgb = edvrSmoothGradients(o.rgb, pos.xy);
    return o;
}
)HLSL";

// Tone and bloom composite: the scene at t0 (its alpha carries through), the bloom at t1. Two inputs, so the vertex stage
// is a float2.
constexpr char kSmoothGradients9270PS[] = EDVR_SMOOTH_GRADIENTS_COMMON_HLSL R"HLSL(
cbuffer CB2 : register(b2) { float4 cb2[11]; };
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);

float4 main(float2 v0 : TEXCOORD1, float4 pos : SV_Position) : SV_Target {
    float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, o = 0.0;
    r0 = t0.Sample(s0, v0.xy);
    r1 = r0 * cb2[2].x;
    r2.xyz = t1.Sample(s1, v0.xy).xyz;
    r2.xyz = r2.xyz * cb2[2].x;
    r0.w = cb2[2].x * 64500.0;
    r2.xyz = max(r2.xyz, 0.0);
    r2.xyz = min(r0.www, r2.xyz);
    [branch] if (cb2[1].w >= 0.0) {
        r3.xyz = mad(-r0.xyz, cb2[2].x, r2.xyz);
        r3.xyz = mad(cb2[1].w, r3.xyz, r1.xyz);
    } else {
        r3.xyz = mad(r0.xyz, cb2[2].x, r2.xyz);
    }
    r0.xyz = max(r3.xyz, 0.0);
    r1.xyz = min(r0.www, r0.xyz);
    r1 = max(r1, 0.0);
    r0 = min(r0.wwww, r1);
    r1.xyz = r0.xyz * cb2[7].x;
    r2.xyz = r1.xyz * cb2[3].x;
    r2.xyz = mad(cb2[3].z, cb2[3].y, r2.xyz);
    r3.xy = cb2[3].w * cb2[4].xy;
    r2.xyz = mad(r1.xyz, r2.xyz, r3.x);
    r3.xzw = mad(cb2[3].x, r1.xyz, cb2[3].y);
    r1.xyz = mad(r1.xyz, r3.xzw, r3.y);
    r1.xyz = r2.xyz / r1.xyz;
    r1.w = cb2[4].x / cb2[4].y;
    r1.xyz = r1.xyz - r1.w;
    r1.xyz = r1.xyz / cb2[4].z;
    r1.xyz = log2(r1.xyz);
    r1.xyz = r1.xyz * cb2[9].z;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = saturate(mad(r1.xyz, cb2[10].y, cb2[9].y));
    r1.w = dot(r1.xyz, float3(0.308600008, 0.609399974, 0.0820000023));
    r1.xyz = r1.xyz - r1.w;
    r1.xyz = mad(cb2[10].z, r1.xyz, r1.w);
    r0.xyz = r1.xyz * cb2[10].x;
    r0 = max(r0, 0.0);
    o = min(r0, 64500.0);
    o.rgb = edvrSmoothGradients(o.rgb, pos.xy);
    return o;
}
)HLSL";

// Stock tone, bloom off: the scene at t1 and the 3D grade at t0.
constexpr char kSmoothGradientsFEE7PS[] = EDVR_SMOOTH_GRADIENTS_COMMON_HLSL R"HLSL(
cbuffer CB2 : register(b2) { float4 cb2[16]; };
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
Texture3D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);

float4 main(float3 v0 : TEXCOORD1, float4 pos : SV_Position) : SV_Target {
    float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, o = 0.0;
    r0 = t1.Sample(s1, v0.xy);
    r0 = r0 * cb2[2].x;
    r1.x = cb2[2].x * 64500.0;
    r0 = max(r0, 0.0);
    r0 = min(r1.xxxx, r0);
    [branch] if (asuint(cb2[8].z)) { r1.x = 1.39999998; } else { r1.x = 0.0; }
    r1.x = r1.x + cb2[8].x;
    r1.y = v0.z * 8.0;
    r1.y = log2(r1.y);
    r1.x = -r1.x + r1.y;
    r1.x = r1.x + -3.0;
    r1.y = cb2[7].w + cb2[7].w;
    r1.x = r1.x * cb2[8].y;
    r1.x = exp2(r1.x);
    r1.x = r1.x + 1.0;
    r1.x = log2(r1.x);
    r1.x = mad(r1.x, 0.301030010, r1.y);
    r1.x = r1.y / r1.x;
    r1.x = log2(r1.x);
    r1.x = r1.x * cb2[7].w;
    r1.x = exp2(r1.x);
    r1.x = mad(cb2[7].z, 1.02999997, -r1.x);
    r1.x = r1.x / v0.z;
    r1.x = log2(r1.x);
    r1.x = r1.x - cb2[7].y;
    r1.x = exp2(r1.x);
    r1.xyz = r0.xyz * r1.x;
    r1.xyz = r1.xyz * cb2[6].y;
    r2.xyz = mad(r1.xyz, 8.46800041, 1.0);
    r2.xyz = mad(r1.xyz, r2.xyz, -0.00295699993);
    r2.xyz = mad(r1.xyz, r2.xyz, 1.00400001e-04);
    r2.xyz = mad(r1.xyz, r2.xyz, -1.27400000e-07);
    r3.xyz = mad(r1.xyz, 8.36040020, 1.82270002);
    r3.xyz = mad(r1.xyz, r3.xyz, 2.18899995e-01);
    r3.xyz = mad(r1.xyz, r3.xyz, -2.11700005e-03);
    r1.xyz = mad(r1.xyz, r3.xyz, 3.67300017e-05);
    r1.xyz = saturate(r2.xyz / r1.xyz);
    r1.xyz = log2(r1.xyz);
    r1.xyz = r1.xyz * cb2[9].z;
    r1.xyz = r1.xyz * 2.20000005;
    r1.xyz = exp2(r1.xyz);
    r1.w = 1.0 - cb2[0].x;
    r2.x = cb2[0].x * 0.5;
    r2.xyz = mad(r1.xyz, r1.w, r2.x);
    r2.xyz = t0.Sample(s0, r2.xyz).xyz;
    r2.xyz = r2.xyz - r1.xyz;
    r1.xyz = mad(cb2[11].x, r2.xyz, r1.xyz);
    r1.xyz = saturate(mad(r1.xyz, cb2[10].y, cb2[9].y));
    r1.w = dot(r1.xyz, float3(0.308600008, 0.609399974, 0.0820000023));
    r1.xyz = r1.xyz - r1.w;
    r1.xyz = mad(cb2[10].z, r1.xyz, r1.w);
    r0.xyz = r1.xyz * cb2[10].x;
    r0 = max(r0, 0.0);
    r0 = min(r0, 64500.0);
    [branch] if (asuint(cb2[15].y)) { r0.xyz = 0.0; }
    o = r0;
    o.rgb = edvrSmoothGradients(o.rgb, pos.xy);
    return o;
}
)HLSL";

// Tone and depth-of-field composite: the scene at t0, the depth-of-field layer at t1.
constexpr char kSmoothGradientsDE65PS[] = EDVR_SMOOTH_GRADIENTS_COMMON_HLSL R"HLSL(
cbuffer CB2 : register(b2) { float4 cb2[16]; };
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);

float4 main(float3 v0 : TEXCOORD1, float4 pos : SV_Position) : SV_Target {
    float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, o = 0.0;
    r0 = t0.Sample(s0, v0.xy);
    r0 = r0 * cb2[2].x;
    r1.x = cb2[2].x * 64500.0;
    r0 = max(r0, 0.0);
    r0 = min(r1.xxxx, r0);
    r1.yzw = t1.Sample(s1, v0.xy).xyz;
    r1.yzw = r1.yzw * cb2[2].x;
    r1.yzw = max(r1.yzw, 0.0);
    r1.yzw = min(r1.xxx, r1.yzw);
    [branch] if (asuint(cb2[8].z)) { r2.x = 1.39999998; } else { r2.x = 0.0; }
    r2.x = r2.x + cb2[8].x;
    r2.y = v0.z * 8.0;
    r2.y = log2(r2.y);
    r2.x = -r2.x + r2.y;
    r2.x = r2.x + -3.0;
    r2.z = cb2[7].w + cb2[7].w;
    r2.x = r2.x * cb2[8].y;
    r2.x = exp2(r2.x);
    r2.x = r2.x + 1.0;
    r2.x = log2(r2.x);
    r2.x = mad(r2.x, 0.301030010, r2.z);
    r2.x = r2.z / r2.x;
    r2.x = log2(r2.x);
    r2.x = r2.x * cb2[7].w;
    r2.x = exp2(r2.x);
    r2.x = mad(cb2[7].z, 1.02999997, -r2.x);
    r2.x = r2.x / v0.z;
    r2.x = log2(r2.x);
    r2.x = r2.x - cb2[7].y;
    r2.x = exp2(r2.x);
    r3.xyz = r0.xyz * r2.x;
    [branch] if (asuint(cb2[8].z)) { r2.x = 1.39999998; } else { r2.x = 0.0; }
    r2.x = r2.x + cb2[8].x;
    r2.x = -r2.x + r2.y;
    r2.x = r2.x + -3.0;
    r2.x = r2.x * cb2[8].y;
    r2.x = exp2(r2.x);
    r2.x = r2.x + 1.0;
    r2.x = log2(r2.x);
    r2.x = mad(r2.x, 0.301030010, r2.z);
    r2.x = r2.z / r2.x;
    r2.x = log2(r2.x);
    r2.x = r2.x * cb2[7].w;
    r2.x = exp2(r2.x);
    r2.x = mad(cb2[7].z, 1.02999997, -r2.x);
    r2.x = r2.x / v0.z;
    r2.x = log2(r2.x);
    r2.x = r2.x - cb2[7].y;
    r2.x = exp2(r2.x);
    [branch] if (cb2[1].w >= 0.0) {
        r2.yzw = mad(r2.x, r1.yzw, -r3.xyz);
        r2.yzw = mad(cb2[1].w, r2.yzw, r3.xyz);
    } else {
        r2.yzw = mad(r2.x, r1.yzw, r3.xyz);
    }
    r1.yzw = max(r2.yzw, 0.0);
    r1.xyz = min(r1.xxx, r1.yzw);
    r1.xyz = r1.xyz * cb2[6].y;
    r2.xyz = mad(r1.xyz, 8.46800041, 1.0);
    r2.xyz = mad(r1.xyz, r2.xyz, -0.00295699993);
    r2.xyz = mad(r1.xyz, r2.xyz, 1.00400001e-04);
    r2.xyz = mad(r1.xyz, r2.xyz, -1.27400000e-07);
    r3.xyz = mad(r1.xyz, 8.36040020, 1.82270002);
    r3.xyz = mad(r1.xyz, r3.xyz, 2.18899995e-01);
    r3.xyz = mad(r1.xyz, r3.xyz, -2.11700005e-03);
    r1.xyz = mad(r1.xyz, r3.xyz, 3.67300017e-05);
    r1.xyz = saturate(r2.xyz / r1.xyz);
    r1.xyz = log2(r1.xyz);
    r1.xyz = r1.xyz * cb2[9].z;
    r1.xyz = r1.xyz * 2.20000005;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = saturate(mad(r1.xyz, cb2[10].y, cb2[9].y));
    r1.w = dot(r1.xyz, float3(0.308600008, 0.609399974, 0.0820000023));
    r1.xyz = r1.xyz - r1.w;
    r1.xyz = mad(cb2[10].z, r1.xyz, r1.w);
    r0.xyz = r1.xyz * cb2[10].x;
    r0 = max(r0, 0.0);
    r0 = min(r0, 64500.0);
    [branch] if (asuint(cb2[15].y)) { r0.xyz = 0.0; }
    o = r0;
    o.rgb = edvrSmoothGradients(o.rgb, pos.xy);
    return o;
}
)HLSL";

// The 3D menu's tone: the scene at t0 only.
constexpr char kSmoothGradients6E83PS[] = EDVR_SMOOTH_GRADIENTS_COMMON_HLSL R"HLSL(
cbuffer CB2 : register(b2) { float4 cb2[16]; };
SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);

float4 main(float3 v0 : TEXCOORD1, float4 pos : SV_Position) : SV_Target {
    float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, o = 0.0;
    r0 = t0.Sample(s0, v0.xy);
    r0 = r0 * cb2[2].x;
    r1.x = cb2[2].x * 64500.0;
    r0 = max(r0, 0.0);
    r0 = min(r1.xxxx, r0);
    [branch] if (asuint(cb2[8].z)) { r1.x = 1.39999998; } else { r1.x = 0.0; }
    r1.x = r1.x + cb2[8].x;
    r1.y = v0.z * 8.0;
    r1.y = log2(r1.y);
    r1.x = -r1.x + r1.y;
    r1.x = r1.x + -3.0;
    r1.y = cb2[7].w + cb2[7].w;
    r1.x = r1.x * cb2[8].y;
    r1.x = exp2(r1.x);
    r1.x = r1.x + 1.0;
    r1.x = log2(r1.x);
    r1.x = mad(r1.x, 0.301030010, r1.y);
    r1.x = r1.y / r1.x;
    r1.x = log2(r1.x);
    r1.x = r1.x * cb2[7].w;
    r1.x = exp2(r1.x);
    r1.x = mad(cb2[7].z, 1.02999997, -r1.x);
    r1.x = r1.x / v0.z;
    r1.x = log2(r1.x);
    r1.x = r1.x - cb2[7].y;
    r1.x = exp2(r1.x);
    r1.xyz = r0.xyz * r1.x;
    r1.xyz = r1.xyz * cb2[6].y;
    r2.xyz = mad(r1.xyz, 8.46800041, 1.0);
    r2.xyz = mad(r1.xyz, r2.xyz, -0.00295699993);
    r2.xyz = mad(r1.xyz, r2.xyz, 1.00400001e-04);
    r2.xyz = mad(r1.xyz, r2.xyz, -1.27400000e-07);
    r3.xyz = mad(r1.xyz, 8.36040020, 1.82270002);
    r3.xyz = mad(r1.xyz, r3.xyz, 2.18899995e-01);
    r3.xyz = mad(r1.xyz, r3.xyz, -2.11700005e-03);
    r1.xyz = mad(r1.xyz, r3.xyz, 3.67300017e-05);
    r1.xyz = saturate(r2.xyz / r1.xyz);
    r1.xyz = log2(r1.xyz);
    r1.xyz = r1.xyz * cb2[9].z;
    r1.xyz = r1.xyz * 2.20000005;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = saturate(mad(r1.xyz, cb2[10].y, cb2[9].y));
    r1.w = dot(r1.xyz, float3(0.308600008, 0.609399974, 0.0820000023));
    r1.xyz = r1.xyz - r1.w;
    r1.xyz = mad(cb2[10].z, r1.xyz, r1.w);
    r0.xyz = r1.xyz * cb2[10].x;
    r0 = max(r0, 0.0);
    r0 = min(r0, 64500.0);
    [branch] if (asuint(cb2[15].y)) { r0.xyz = 0.0; }
    o = r0;
    o.rgb = edvrSmoothGradients(o.rgb, pos.xy);
    return o;
}
)HLSL";

#undef EDVR_SMOOTH_GRADIENTS_COMMON_HLSL

}  // namespace edvr
