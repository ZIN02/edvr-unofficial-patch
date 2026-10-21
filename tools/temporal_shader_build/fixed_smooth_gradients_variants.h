#pragma once
// Build-only registry: the smooth gradients' five tone transcriptions (src/d3d11/smooth_gradients_ps.h). New, so held to their own
// contract (symbol, source name, entry, profile, the text they compile, and the bindings their DXBC reflects) rather than a former
// runtime call's; tools/smooth_gradients_test holds their behaviour against the game's own bytecode. Production text: no macro, so
// the dither is in.
#include "../../src/d3d11/smooth_gradients_ps.h"
static std::vector<Variant> smoothGradientsVariants() { return {
    {"kSmoothGradientsE894Bytecode","smooth_gradients_E894_ps","main",nullptr,{},false,edvr::kSmoothGradientsE894PS,"ps_5_0"},
    {"kSmoothGradients9270Bytecode","smooth_gradients_9270_ps","main",nullptr,{},false,edvr::kSmoothGradients9270PS,"ps_5_0"},
    {"kSmoothGradientsFEE7Bytecode","smooth_gradients_FEE7_ps","main",nullptr,{},false,edvr::kSmoothGradientsFEE7PS,"ps_5_0"},
    {"kSmoothGradientsDE65Bytecode","smooth_gradients_DE65_ps","main",nullptr,{},false,edvr::kSmoothGradientsDE65PS,"ps_5_0"},
    {"kSmoothGradients6E83Bytecode","smooth_gradients_6E83_ps","main",nullptr,{},false,edvr::kSmoothGradients6E83PS,"ps_5_0"},
}; }
