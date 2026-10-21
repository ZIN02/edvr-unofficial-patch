#pragma once
// Build-only registry: the flat HUD's bloom feed pass (src/d3d11/fixed_shader_source.h kUiGlowFeedPsHlsl, ui_layer.cpp uiLayerGlowFeed). New, so held to
// its own contract (symbol, source name, entry, profile, the text it compiles) rather than a former runtime call's. It reuses the coverage pass's
// fullscreen vertex shader (kUiLayerCoverageVsBytecode).
static std::vector<Variant> glowFeedVariants() { return {
    {"kUiGlowFeedPsBytecode","ui_glow_feed_ps","main",nullptr,{},false,edvr::kUiGlowFeedPsHlsl,"ps_5_0"},
}; }
