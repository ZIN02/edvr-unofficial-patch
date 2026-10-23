// main's depth tile (src/d3d11/temporal_shader_source.h, EDVR_MAIN_DEPTH_TILE): the native-TAA pass's history fetch
// read the 3x3 of zSceneAt around every pixel (and again for the instrument's two depth candidates); it now reads a
// 10x10 group tile filled once at the top of main. The claim, on WARP, bit for bit:
//
//   the production main (both embedded blobs, temporal_aa_cs and temporal_aa_fast_cs) and the same text compiled
//   with EDVR_MAIN_DEPTH_TILE 0 write the same bytes to every output (O, N, UN, Stats) for the same inputs.
//
// The reference is the shader before the tile: fxc gives EDVR_MAIN_DEPTH_TILE 0 the bytecode of origin/main's main
// byte for byte, both variants (checked by hand 2026-10-23, docs/edvr-gpu-trims-2026-10-14.md). The inputs: a region
// offset into its texture and not a multiple of 8 either way (partial groups, the clamped edges), a depth map of
// sky, far and near surfaces with a few signed zeros, denormals and NaNs, the smoke's and the UI depth bound or
// not, the screen map, the coverage mask and the previous UI colour; the configurations: depth bound or not, the
// head's depth term and the world path each on or off, six probe.w words, and no history.
// Non-vacuity: the world path ran where asked (Stats[15]), the depth changes the outputs, WARP agrees with
// itself, and a mutant tile index (every tap the pixel's own entry) is caught, and only where the 3x3 runs.
// By hand: ui_depth_test --main-tile [--adapter nvidia] [--bench] (the bench: temporal_aa_fast_cs at 2016x2016 against the
// untiled main, zscene_gate_test.h's harness).
#pragma once
#include <thread>

namespace maintile {
using namespace zgate;

constexpr UINT kTexW = 48, kTexH = 40, kRegX = 5, kRegY = 3, kW = 37, kH = 29;

struct Rig {
    ComPtr<ID3D11Buffer> cb, stats, statsStage;
    ComPtr<ID3D11Texture2D> o, n, un;
    ComPtr<ID3D11ShaderResourceView> s, h, z, zp, um, zs, zu, up, screen;
    ComPtr<ID3D11UnorderedAccessView> oU, nU, unU, statsU;
    ComPtr<ID3D11SamplerState> samp;
};
uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
float unit(uint32_t x) { return float(hash32(x) & 0xFFFFFFu) / float(0xFFFFFF); }

struct Cfg { int knobsY, tvUsedW, tvCamW, haveHistory; uint32_t probeW; };
std::string cfgText(const Cfg& c) {
    char b[120];
    std::snprintf(b, sizeof(b), "knobs.y %d, tvUsed.w %d, tvCam.w %d, haveHistory %d, probe.w %u", c.knobsY, c.tvUsedW, c.tvCamW, c.haveHistory, c.probeW);
    return b;
}

std::vector<char> buildParams(ID3D11ShaderReflectionConstantBuffer* P, UINT size, const Cfg& c) {
    std::vector<char> cb(size, 0);
    auto put = [&](const char* name, const void* v, UINT bytes) {
        D3D11_SHADER_VARIABLE_DESC vd{};
        check(SUCCEEDED(P->GetVariableByName(name)->GetDesc(&vd)) && bytes <= vd.Size && vd.StartOffset + bytes <= cb.size(),
              "main depth tile: cbuffer P field");
        std::memcpy(cb.data() + vd.StartOffset, v, bytes);
    };
    auto f4 = [&](const char* name, float a, float b, float cc, float d) { const float v[4] = {a, b, cc, d}; put(name, v, 16); };
    auto rows = [&](const char* r0, const char* r1, const char* r2, float yaw, float pitch) {
        f4(r0, 1, 0, -yaw, 0); f4(r1, 0, 1, -pitch, 0); f4(r2, yaw, pitch, 1, 0);
    };
    const int region[4] = {int(kRegX), int(kRegY), int(kRegX + kW), int(kRegY + kH)}, sz[2] = {int(kW), int(kH)}, tex[2] = {int(kTexW), int(kTexH)};
    put("region", region, 16); put("size", sz, 8); put("texSize", tex, 8);
    f4("tanNow", -1.1f, 0.9f, -0.8f, 1.0f); f4("tanPrev", -1.12f, 0.88f, -0.79f, 1.01f); f4("jit", .31f, -.18f, 1, .75f);
    rows("dR0", "dR1", "dR2", .012f, -.007f);
    rows("c0R0", "c0R1", "c0R2", .010f, -.006f);
    rows("c1R0", "c1R1", "c1R2", .014f, -.008f);
    rows("c2R0", "c2R1", "c2R2", .031f, .011f);
    rows("c3R0", "c3R1", "c3R2", .012f, -.007f);
    const float blend = .9f, gamma = 1.25f;
    const int candMask = 15;
    put("blend", &blend, 4); put("gamma", &gamma, 4); put("haveHistory", &c.haveHistory, 4); put("candMask", &candMask, 4);
    f4("knobs", .0001f, float(c.knobsY), .025f, 1000);
    f4("tvUsed", .012f, -.007f, .02f, float(c.tvUsedW)); f4("tvCand", -.03f, .002f, .01f, 0); f4("tvCam", .35f, -.12f, .9f, float(c.tvCamW));
    f4("split", 2, 0, 0, 0);
    const bool coverage = (c.probeW & (4u | 128u)) != 0;
    f4("probe", 1, 1, coverage ? 1.0f : 0.0f, float(c.probeW));
    f4("holoJitter", .15f, -.1f, 1, 1);
    return cb;
}

struct Outputs { std::vector<uint32_t> o, n, un, stats; };
bool same(const Outputs& a, const Outputs& b) { return a.o == b.o && a.n == b.n && a.un == b.un && a.stats == b.stats; }
std::string firstDiff(const Outputs& a, const Outputs& b) {
    const std::pair<const char*, std::pair<const std::vector<uint32_t>*, const std::vector<uint32_t>*>> outs[] = {
        {"O", {&a.o, &b.o}}, {"N", {&a.n, &b.n}}, {"UN", {&a.un, &b.un}}, {"Stats", {&a.stats, &b.stats}}};
    for (const auto& x : outs)
        for (size_t i = 0; i < x.second.first->size(); ++i)
            if ((*x.second.first)[i] != (*x.second.second)[i]) {
                char text[160];
                const size_t texel = std::strcmp(x.first, "Stats") ? i / 4 : i;
                std::snprintf(text, sizeof(text), "%s word %zu (texel %zu, x %zu y %zu): %08X vs %08X", x.first, i, texel, texel % kW, texel / kW,
                              (*x.second.first)[i], (*x.second.second)[i]);
                return text;
            }
    return "none";
}

Rig makeRig(ID3D11Device* dev, ID3D11DeviceContext* ctx, UINT cbSize) {
    Rig r;
    const size_t texN = size_t(kTexW) * kTexH, regN = size_t(kW) * kH;
    std::vector<float> s(texN * 4), h(regN * 4), up(regN * 4), um(regN), zp(regN), screen(texN * 4, 0.0f);
    std::vector<uint32_t> z(texN), zs(texN, 0u), zu(texN);
    for (size_t i = 0; i < texN; ++i) {
        for (int c = 0; c < 4; ++c) s[i * 4 + c] = unit(uint32_t(i * 4 + c) ^ 0x51u);
        const UINT x = UINT(i % kTexW), y = UINT(i / kTexW);
        const uint32_t k = hash32(uint32_t(i) ^ 0xA5A5u);
        float d;
        if (y < kTexH / 4 || (k & 7u) == 0) d = 0.0f;                                   // sky
        else if (x > kTexW * 2 / 3 && y > kTexH / 2) d = .025f / (.3f + 1.2f * unit(k));   // the hull, near
        else d = .025f / (5.0f + 4995.0f * unit(k + 1));                                  // a surface past the split
        std::memcpy(&z[i], &d, 4);
        if ((k & 0x3FFu) == 1) z[i] = 0x80000000u;   // -0
        if ((k & 0x3FFu) == 2) z[i] = 0x00000001u;   // a denormal
        if ((k & 0x3FFu) == 3) z[i] = 0x7FC00000u;   // NaN
        if (x > 10 && x < 20 && y > 12 && y < 22) { const float smoke = .025f / 3.0f; std::memcpy(&zs[i], &smoke, 4); }
        const float panel = .025f / .6f;
        if (y > 25 && y < 30 && x > 8 && x < 40) std::memcpy(&zu[i], &panel, 4); else zu[i] = z[i];
        if (x > 30 && x < 38 && y > 4 && y < 12) {   // the screen map: tracked, and source UI history (w 2) at its corner
            screen[i * 4] = .4f; screen[i * 4 + 1] = -.3f; screen[i * 4 + 2] = .025f / .9f; screen[i * 4 + 3] = (x > 35 && y > 9) ? 2.0f : 1.0f;
        }
    }
    for (size_t i = 0; i < regN; ++i) {
        for (int c = 0; c < 4; ++c) h[i * 4 + c] = unit(uint32_t(i * 4 + c) ^ 0x1234u);
        for (int c = 0; c < 3; ++c) up[i * 4 + c] = unit(uint32_t(i * 4 + c) ^ 0x777u);
        up[i * 4 + 3] = (hash32(uint32_t(i) ^ 0x99u) & 3u) == 0 ? 1.0f : 0.0f;
        um[i] = float(hash32(uint32_t(i) ^ 0x4242u) & 0xFFu) / 255.0f;
        zp[i] = .025f / (2.0f + 50.0f * unit(uint32_t(i) ^ 0x31u));
    }
    auto f32 = [&](UINT w, UINT hh, DXGI_FORMAT fmt, const void* data, UINT bpp) {
        return srvOf(dev, tex2d(dev, w, hh, fmt, D3D11_BIND_SHADER_RESOURCE, data, w * bpp).Get());
    };
    r.s = f32(kTexW, kTexH, DXGI_FORMAT_R32G32B32A32_FLOAT, s.data(), 16);
    r.h = f32(kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, h.data(), 16);
    r.z = f32(kTexW, kTexH, DXGI_FORMAT_R32_FLOAT, z.data(), 4);
    r.zp = f32(kW, kH, DXGI_FORMAT_R32_FLOAT, zp.data(), 4);
    r.um = f32(kW, kH, DXGI_FORMAT_R32_FLOAT, um.data(), 4);
    r.zs = f32(kTexW, kTexH, DXGI_FORMAT_R32_FLOAT, zs.data(), 4);
    r.zu = f32(kTexW, kTexH, DXGI_FORMAT_R32_FLOAT, zu.data(), 4);
    r.up = f32(kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, up.data(), 16);
    r.screen = f32(kTexW, kTexH, DXGI_FORMAT_R32G32B32A32_FLOAT, screen.data(), 16);
    r.o = tex2d(dev, kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, nullptr, 0);
    r.n = tex2d(dev, kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, nullptr, 0);
    r.un = tex2d(dev, kW, kH, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, nullptr, 0);
    r.oU = uavOf(dev, r.o.Get()); r.nU = uavOf(dev, r.n.Get()); r.unU = uavOf(dev, r.un.Get());
    D3D11_BUFFER_DESC sb{}; sb.ByteWidth = 128 * 4; sb.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    sb.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; sb.StructureByteStride = 4;
    check(SUCCEEDED(dev->CreateBuffer(&sb, nullptr, &r.stats)), "main depth tile: stats buffer");
    r.statsU = uavOf(dev, r.stats.Get());
    sb.BindFlags = 0; sb.MiscFlags = 0; sb.StructureByteStride = 0; sb.Usage = D3D11_USAGE_STAGING; sb.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(dev->CreateBuffer(&sb, nullptr, &r.statsStage)), "main depth tile: stats staging");
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = cbSize; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &r.cb)), "main depth tile: constants");
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    check(SUCCEEDED(dev->CreateSamplerState(&sd, &r.samp)), "main depth tile: sampler");
    (void)ctx;
    return r;
}

Outputs run(ID3D11Device* dev, ID3D11DeviceContext* ctx, Rig& r, ID3D11ComputeShader* cs, const std::vector<char>& params, bool layers) {
    ctx->UpdateSubresource(r.cb.Get(), 0, nullptr, params.data(), 0, 0);
    const UINT sentinel[4] = {0x7F7F7F7Fu, 0x7F7F7F7Fu, 0x7F7F7F7Fu, 0x7F7F7F7Fu}, zero[4] = {};
    for (auto* u : {r.oU.Get(), r.nU.Get(), r.unU.Get()}) ctx->ClearUnorderedAccessViewUint(u, sentinel);
    ctx->ClearUnorderedAccessViewUint(r.statsU.Get(), zero);
    ctx->ClearState();
    ID3D11ShaderResourceView* srvs[15] = {r.s.Get(), r.h.Get(), r.z.Get(), r.zp.Get(), r.um.Get(), nullptr,
                                          layers ? r.zs.Get() : nullptr, layers ? r.zu.Get() : nullptr, r.up.Get(),
                                          nullptr, nullptr, nullptr, nullptr, nullptr, r.screen.Get()};
    ctx->CSSetShaderResources(0, 15, srvs);
    ID3D11UnorderedAccessView* uavs[7] = {r.oU.Get(), r.nU.Get(), r.statsU.Get(), nullptr, nullptr, nullptr, r.unU.Get()};
    ctx->CSSetUnorderedAccessViews(0, 7, uavs, nullptr);
    ctx->CSSetConstantBuffers(0, 1, r.cb.GetAddressOf());
    ctx->CSSetSamplers(0, 1, r.samp.GetAddressOf());
    ctx->CSSetShader(cs, nullptr, 0);
    ctx->Dispatch((kW + 7) / 8, (kH + 7) / 8, 1);
    ctx->ClearState();
    Outputs out;
    out.o = readU32(dev, ctx, r.o.Get());
    out.n = readU32(dev, ctx, r.n.Get());
    out.un = readU32(dev, ctx, r.un.Get());
    ctx->CopyResource(r.statsStage.Get(), r.stats.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    check(SUCCEEDED(ctx->Map(r.statsStage.Get(), 0, D3D11_MAP_READ, 0, &m)), "main depth tile: stats map");
    out.stats.assign(static_cast<const uint32_t*>(m.pData), static_cast<const uint32_t*>(m.pData) + 128);
    ctx->Unmap(r.statsStage.Get(), 0);
    return out;
}

// The A/B on the given device (WARP in the build). Returns the configurations compared.
int abTest(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    static const D3D_SHADER_MACRO refFast[] = {{"EDVR_TEMPORAL_DIAGNOSTICS", "0"}, {"EDVR_MAIN_DEPTH_TILE", "0"}, {nullptr, nullptr}};
    static const D3D_SHADER_MACRO refDiag[] = {{"EDVR_TEMPORAL_DIAGNOSTICS", "1"}, {"EDVR_MAIN_DEPTH_TILE", "0"}, {nullptr, nullptr}};
    static const D3D_SHADER_MACRO fast[] = {{"EDVR_TEMPORAL_DIAGNOSTICS", "0"}, {nullptr, nullptr}};
    const std::string hlsl = temporalHlsl(temporalSource());
    // The mutant: every tap of the tile read is the pixel's own entry (the 3x3 collapsed; it stays inside the tile).
    const char* kAnchor = "const int2 t = int2(p) - gMainDepthTileOrigin + int2(ox, oy) + 1;";
    const size_t at = hlsl.find(kAnchor);
    check(at != std::string::npos && hlsl.find(kAnchor, at + 1) == std::string::npos, "main depth tile: the tile index found exactly once");
    std::string mutant = hlsl;
    mutant.replace(at, std::strlen(kAnchor), "const int2 t = int2(p) - gMainDepthTileOrigin + int2(0, 0) + 1;");
    // The three compiles together: main alone is ~20 s in fxc (production flags: cs_5_0, flags 0).
    ComPtr<ID3DBlob> refDiagCode, refFastCode, mutantCode;
    {
        std::thread a([&] { refDiagCode = compileEntry(hlsl, "main", refDiag, 0); });
        std::thread b([&] { refFastCode = compileEntry(hlsl, "main", refFast, 0); });
        std::thread c([&] { mutantCode = compileEntry(mutant, "main", fast, 0); });
        a.join(); b.join(); c.join();
    }
    auto refDiagCs = makeCs(dev, refDiagCode->GetBufferPointer(), refDiagCode->GetBufferSize());
    auto refFastCs = makeCs(dev, refFastCode->GetBufferPointer(), refFastCode->GetBufferSize());
    auto mutantCs = makeCs(dev, mutantCode->GetBufferPointer(), mutantCode->GetBufferSize());
    auto newDiagCs = makeCs(dev, edvr::kTemporalAaBytecode, sizeof(edvr::kTemporalAaBytecode));
    auto newFastCs = makeCs(dev, edvr::kTemporalAaFastBytecode, sizeof(edvr::kTemporalAaFastBytecode));
    check(refDiagCode->GetBufferSize() != sizeof(edvr::kTemporalAaBytecode) ||
              std::memcmp(refDiagCode->GetBufferPointer(), edvr::kTemporalAaBytecode, sizeof(edvr::kTemporalAaBytecode)) != 0,
          "main depth tile: the embedded temporal_aa_cs is not the untiled reference (the tile is in the shipping blob)");
    ComPtr<ID3D11ShaderReflection> refl;
    check(SUCCEEDED(D3DReflect(edvr::kTemporalAaBytecode, sizeof(edvr::kTemporalAaBytecode), __uuidof(ID3D11ShaderReflection), &refl)),
          "main depth tile: reflection");
    auto* P = refl->GetConstantBufferByName("P");
    D3D11_SHADER_BUFFER_DESC pd{};
    check(SUCCEEDED(P->GetDesc(&pd)), "main depth tile: cbuffer P");
    Rig r = makeRig(dev, ctx, pd.Size);

    std::vector<Cfg> cfgs;
    for (int ky : {1, 0}) for (int tu : {1, 0}) for (int tc : {1, 0})
        for (uint32_t pw : {0u, 1u, 32u, 128u, 6u, 64u | 256u}) cfgs.push_back({ky, tu, tc, 1, pw});
    cfgs.push_back({1, 1, 1, 0, 0u});
    cfgs.push_back({1, 1, 1, 0, 32u});
    int compared = 0, mutantCaught = 0, depthMatters = 0;
    char label[320];
    std::vector<Outputs> byDepth[2];
    for (const Cfg& c : cfgs) {
        const bool layers = (c.probeW & (64u | 256u)) == 0;   // the bits say exactly what is bound
        const auto params = buildParams(P, pd.Size, c);
        const std::string where = cfgText(c);
        const Outputs nd = run(dev, ctx, r, newDiagCs.Get(), params, layers), nd2 = run(dev, ctx, r, newDiagCs.Get(), params, layers);
        const Outputs od = run(dev, ctx, r, refDiagCs.Get(), params, layers);
        const Outputs nf = run(dev, ctx, r, newFastCs.Get(), params, layers), of = run(dev, ctx, r, refFastCs.Get(), params, layers);
        const Outputs mf = run(dev, ctx, r, mutantCs.Get(), params, layers);
        std::snprintf(label, sizeof(label), "main depth tile [%s]: WARP is deterministic (one shader, one input, one result)", where.c_str());
        check(same(nd, nd2), label);
        if (!same(od, nd)) std::printf("main depth tile [%s] diagnostic: first difference %s\n", where.c_str(), firstDiff(od, nd).c_str());
        std::snprintf(label, sizeof(label), "main depth tile [%s]: temporal_aa_cs and the untiled reference write the same bytes to O, N, UN and Stats", where.c_str());
        check(same(od, nd), label);
        if (!same(of, nf)) std::printf("main depth tile [%s] fast: first difference %s\n", where.c_str(), firstDiff(of, nf).c_str());
        std::snprintf(label, sizeof(label), "main depth tile [%s]: temporal_aa_fast_cs and the untiled reference write the same bytes to O, N, UN and Stats", where.c_str());
        check(same(of, nf), label);
        bool written = true;
        for (uint32_t v : nf.n) if (v == 0x7F7F7F7Fu) written = false;
        std::snprintf(label, sizeof(label), "main depth tile [%s]: every pixel of N written", where.c_str());
        check(written, label);
        const bool world = c.knobsY && c.tvCamW && c.haveHistory;
        std::snprintf(label, sizeof(label), "main depth tile [%s]: the world path ran exactly where asked (Stats[15] = %u)", where.c_str(), nd.stats[15]);
        check((nd.stats[15] > 0) == world, label);
        const bool runs3x3 = c.knobsY && (c.tvUsedW || c.tvCamW) && c.haveHistory;   // the fast blob: the used fetch only
        if (!same(of, mf)) ++mutantCaught;
        std::snprintf(label, sizeof(label), "main depth tile [%s]: the mutant tile index is caught exactly where the 3x3 runs", where.c_str());
        check(runs3x3 == !same(of, mf), label);
        if (c.haveHistory && c.tvUsedW && c.tvCamW && c.probeW == 0) byDepth[c.knobsY].push_back(nf);
        compared += 4;
    }
    if (!byDepth[0].empty() && !byDepth[1].empty() && !same(byDepth[0][0], byDepth[1][0])) ++depthMatters;
    check(depthMatters > 0, "main depth tile: the scene depth changes the outputs (the A/B sees the 3x3)");
    std::snprintf(label, sizeof(label), "main depth tile: the mutant tile index (the 3x3 collapsed) is caught (%d configurations)", mutantCaught);
    check(mutantCaught > 0, label);
    std::printf("PASS: main depth tile: temporal_aa_cs and temporal_aa_fast_cs byte-identical to the untiled main over %zu configurations "
                "(%d dispatch pairs; %ux%u at (%u,%u) in %ux%u); the mutant tile index caught in %d\n",
                cfgs.size(), compared, kW, kH, kRegX, kRegY, kTexW, kTexH, mutantCaught);
    return int(cfgs.size());
}

// One temporal entry's inputs at the eye size, its parameters written through cbuffer P's reflected layout.
struct BenchFrame {
    UINT w = 2016, h = 2016;
    ComPtr<ID3D11Buffer> cb, stats;
    ComPtr<ID3D11ShaderResourceView> s, hist, z, zp, zs, zu;
    ComPtr<ID3D11UnorderedAccessView> o, n, statsU, mv, zc, mk;
    std::vector<char> params;
    size_t probeW = 0;
};
BenchFrame makeFrame(ID3D11Device* dev, const void* code, size_t size) {
    BenchFrame f;
    const UINT w = f.w, h = f.h;
    ComPtr<ID3D11ShaderReflection> refl;
    check(SUCCEEDED(D3DReflect(code, size, __uuidof(ID3D11ShaderReflection), &refl)), "zscene gate: reflection");
    auto* P = refl->GetConstantBufferByName("P");
    D3D11_SHADER_BUFFER_DESC pd{};
    check(SUCCEEDED(P->GetDesc(&pd)), "zscene gate: cbuffer P");
    f.params.assign(pd.Size, 0);
    auto put = [&](const char* name, const void* v, UINT bytes) {
        D3D11_SHADER_VARIABLE_DESC vd{};
        check(SUCCEEDED(P->GetVariableByName(name)->GetDesc(&vd)) && bytes <= vd.Size && vd.StartOffset + bytes <= f.params.size(),
              "zscene gate: cbuffer P field");
        std::memcpy(f.params.data() + vd.StartOffset, v, bytes);
        if (!std::strcmp(name, "probe")) f.probeW = vd.StartOffset + 12;
    };
    auto f4 = [&](const char* name, float a, float b, float c, float d) { const float v[4] = {a, b, c, d}; put(name, v, 16); };
    const int region[4] = {0, 0, int(w), int(h)}, sz[2] = {int(w), int(h)}, one = 1, none = 0;
    put("region", region, 16); put("size", sz, 8); put("texSize", sz, 8);
    f4("tanNow", -1, 1, -1, 1); f4("tanPrev", -1.002f, 0.998f, -1, 1); f4("jit", .25f, -.375f, 0, .5f);
    f4("dR0", 1, 0, -.002f, 0); f4("dR1", 0, 1, 0, 0); f4("dR2", .002f, 0, 1, 0);
    f4("c2R0", 1, 0, -.003f, 0); f4("c2R1", 0, 1, 0, 0); f4("c2R2", .003f, 0, 1, 0);
    const float blend = .9f, gamma = 1.25f;
    put("blend", &blend, 4); put("gamma", &gamma, 4); put("haveHistory", &one, 4); put("candMask", &none, 4);
    f4("knobs", 0, 1, .025f, 0); f4("tvUsed", .001f, 0, 0, 1); f4("tvCam", .01f, 0, 0, 1); f4("split", 2, 0, 0, 0);
    f4("probe", 1, 0, 0, 0); f4("holoJitter", .25f, -.375f, 1, 1);
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = pd.Size; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &f.cb)), "zscene gate: bench constants");
    // A scene: a third sky, the rest a slanted surface 0.5-20 m away; the smoke's depth a patch; the UI depth
    // the scene with a nearer panel stamped in it (what uiDepthTemporalDepth publishes).
    std::vector<uint32_t> rgba(size_t(w) * h);
    std::vector<float> z(size_t(w) * h), smoke(size_t(w) * h, 0.0f), ui(size_t(w) * h);
    uint32_t seed = 0x9E3779B9u;
    for (UINT y = 0; y < h; ++y) for (UINT x = 0; x < w; ++x) {
        const size_t i = size_t(y) * w + x;
        seed = seed * 1664525u + 1013904223u;
        rgba[i] = seed | 0xFF000000u;
        z[i] = y < h / 3 ? 0.0f : .025f / (.5f + 19.5f * float((x + y) % 997) / 997.0f);
        if (x > w / 3 && x < w / 2 && y > h / 2) smoke[i] = .025f / 3.0f;
        ui[i] = (x > w / 4 && x < 3 * w / 4 && y > 3 * h / 4) ? .025f / .6f : z[i];
    }
    auto sT = tex2d(dev, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, rgba.data(), w * 4);
    auto hT = tex2d(dev, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, nullptr, 0);
    auto zT = tex2d(dev, w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, z.data(), w * 4);
    auto zpT = tex2d(dev, w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, z.data(), w * 4);
    auto zsT = tex2d(dev, w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, smoke.data(), w * 4);
    auto zuT = tex2d(dev, w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, ui.data(), w * 4);
    f.s = srvOf(dev, sT.Get()); f.hist = srvOf(dev, hT.Get()); f.z = srvOf(dev, zT.Get()); f.zp = srvOf(dev, zpT.Get());
    f.zs = srvOf(dev, zsT.Get()); f.zu = srvOf(dev, zuT.Get());
    f.o = uavOf(dev, tex2d(dev, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_UNORDERED_ACCESS, nullptr, 0).Get());
    f.n = uavOf(dev, tex2d(dev, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_UNORDERED_ACCESS, nullptr, 0).Get());
    f.mv = uavOf(dev, tex2d(dev, w, h, DXGI_FORMAT_R32G32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, nullptr, 0).Get());
    f.zc = uavOf(dev, tex2d(dev, w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, nullptr, 0).Get());
    f.mk = uavOf(dev, tex2d(dev, w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS, nullptr, 0).Get());
    D3D11_BUFFER_DESC sb{}; sb.ByteWidth = 128 * 4; sb.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    sb.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; sb.StructureByteStride = 4;
    check(SUCCEEDED(dev->CreateBuffer(&sb, nullptr, &f.stats)), "zscene gate: stats buffer");
    f.statsU = uavOf(dev, f.stats.Get());
    return f;
}
// One bench case: a shader, whether it is the mv entry (else main), which layers are bound and the probe.w bits.
struct BenchCase { const char* name; ID3D11ComputeShader* cs; bool mvEntry; bool t6, t7; uint32_t bits; };
void bindCase(ID3D11DeviceContext* ctx, BenchFrame& f, const BenchCase& c) {
    ctx->ClearState();
    float w; std::memcpy(&w, f.params.data() + f.probeW, 4);
    const float bits = static_cast<float>(c.bits);
    std::memcpy(f.params.data() + f.probeW, &bits, 4);
    ctx->UpdateSubresource(f.cb.Get(), 0, nullptr, f.params.data(), 0, 0);
    std::memcpy(f.params.data() + f.probeW, &w, 4);
    ID3D11ShaderResourceView* srvs[8] = {f.s.Get(), c.mvEntry ? nullptr : f.hist.Get(), f.z.Get(), c.mvEntry ? f.zp.Get() : nullptr,
                                         nullptr, nullptr, c.t6 ? f.zs.Get() : nullptr, c.t7 ? f.zu.Get() : nullptr};
    ctx->CSSetShaderResources(0, 8, srvs);
    ID3D11UnorderedAccessView* uavs[6] = {c.mvEntry ? nullptr : f.o.Get(), c.mvEntry ? nullptr : f.n.Get(), f.statsU.Get(),
                                          c.mvEntry ? f.mv.Get() : nullptr, c.mvEntry ? f.zc.Get() : nullptr, c.mvEntry ? f.mk.Get() : nullptr};
    ctx->CSSetUnorderedAccessViews(0, 6, uavs, nullptr);
    ctx->CSSetConstantBuffers(0, 1, f.cb.GetAddressOf());
    ctx->CSSetShader(c.cs, nullptr, 0);
}
// --bench: the native-TAA pass at the eye size, the production fast main against the untiled one.
void bench(Device& d) {
    static const D3D_SHADER_MACRO refFast[] = {{"EDVR_TEMPORAL_DIAGNOSTICS", "0"}, {"EDVR_MAIN_DEPTH_TILE", "0"}, {nullptr, nullptr}};
    const std::string hlsl = temporalHlsl(temporalSource());
    auto refCode = compileEntry(hlsl, "main", refFast, 0);
    auto prodCs = makeCs(d.dev.Get(), edvr::kTemporalAaFastBytecode, sizeof(edvr::kTemporalAaFastBytecode));
    auto refCs = makeCs(d.dev.Get(), refCode->GetBufferPointer(), refCode->GetBufferSize());
    // The frame's constants come from the diagnostic blob's reflection, which keeps the whole of cbuffer P.
    BenchFrame f = makeFrame(d.dev.Get(), edvr::kTemporalAaBytecode, sizeof(edvr::kTemporalAaBytecode));
    ID3D11DeviceContext* ctx = d.ctx.Get();
    auto arm = [&](const BenchCase& c) { return std::function<void()>([ctx, &f, c] { bindCase(ctx, f, c); }); };
    const UINT gx = (f.w + 7) / 8, gy = (f.h + 7) / 8;
    std::printf("== temporal_aa_fast_cs on %s, %ux%u one eye, depth bound, the head's depth term and the world path on; paired "
                "ABBA/BAAB, %u rounds of 4 blocks of %u; ms per dispatch, untiled -> tiled, median paired saving\n",
                d.name.c_str(), f.w, f.h, zgate::kPairedRounds, zgate::kPairedDispatches);
    printPaired("t6/t7 bound", pairedRounds(d, arm({"", refCs.Get(), false, true, true, 0u}), arm({"", prodCs.Get(), false, true, true, 0u}), gx, gy));
    printPaired("t6/t7 null, bits 64|256",
                pairedRounds(d, arm({"", refCs.Get(), false, false, false, 320u}), arm({"", prodCs.Get(), false, false, false, 320u}), gx, gy));
    if (d.warp) std::puts("note: --bench on WARP times a CPU; use --adapter nvidia.");
}

// ui_depth_test --main-tile [--adapter nvidia] [--bench]: this alone, on the adapter asked for.
int runAlone(int argc, char** argv) {
    bool nvidia = false, wantBench = false;
    if (!aloneFlags(argc, argv, nvidia, wantBench)) { std::puts("usage: ui_depth_test --main-tile [--adapter nvidia] [--bench]"); return 2; }
    Device d = makeDevice(nvidia);
    abTest(d.dev.Get(), d.ctx.Get());
    if (wantBench) maintile::bench(d);
    return 0;
}

}  // namespace maintile
