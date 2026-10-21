// The smooth gradients' five tone transcriptions (src\d3d11\smooth_gradients_ps.h) against the game's own bytecode, on a WARP device with the
// D3D debug layer. Nothing is granted on a likeness:
//   F1 identity: each fixture (tools\smooth_gradients_test\fixtures, copied from edvr_logs\shaders) hashes, FNV-1a64 over the whole file,
//      to its file name.
//   A  equivalence: for each shader and each of four constant sets, the original and the transcription compiled with EDVR_TONE_DITHER=0
//      draw one full-screen triangle into R32G32B32A32_FLOAT on the same inputs: every texture slot the shader declares, filled per slot (the 3D grade smooth, as a game's is: see texels3d);
//      linear clamp at every sampler slot; cb2 from a fixed generator; TEXCOORD1 ramping across the screen (z from 0.5 to 4). Every channel
//      of every pixel must agree (both NaN, or |a-b| <= 1e-4 * max(1,|a|)), and at least 90% of the original's outputs must be finite.
//      The tone curve must also be live: in sets 0, 1 and 3, at least a quarter of the pixels have an RGB channel strictly inside
//      (0.001, 0.999), so the check cannot pass on a curve that is a constant. Set 2 turns the blackout on and is black by design.
//      The four sets: 0, the branch flag at +0.5 with the exposure flag at 0; 1, the flag at -0.5 and the exposure flag at 1; 2, the flag
//      at +0.5, the exposure flag at 1 and the blackout on; 3, the flag at -0.5 with the 2D textures at 64000..65500, so the 64500 clamp
//      binds (the 0..8 inputs cannot reach it).
//   B  dither: for each shader and sets 0 and 2, the production transcription (no macro, so the dither is in) draws into R8G8B8A8_UNORM and
//      the no-dither transcription into R32G32B32A32_FLOAT (the reference) on the same inputs. Per RGB channel |q - clamp(ref,0,1)*255| <= 1.6 (the dither's 1 LSB plus the 0.6 ULP the D3D float-to-UNORM conversion may round by);
//      the 16x16 block means of q and of clamp(ref)*255 differ by at most 0.5; a channel whose reference is exactly 0 is 0 in q; alpha
//      equals the no-dither transcription's alpha after the same 8-bit conversion (the dither must not touch it), and is also checked
//      within 0.6 of clamp(ref.a)*255 (that same conversion tolerance: a device may write 181 for 181.506). Set 0 has no exact zeros, so set 2 (black RGB) is the one that checks the zero rule.
//   S  sources: the build registry (tools\temporal_shader_build\fixed_smooth_gradients_variants.h) names the five symbols, and the header
//      declares the five literals.
// The inputs come from a fixed 32-bit LCG, and the textures are rounded to half precision as they are stored, so every run sees the same values.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "smooth_gradients_ps.h"

using Microsoft::WRL::ComPtr;
using DirectX::PackedVector::XMConvertFloatToHalf;

static const UINT kW = 64;
static const UINT kH = 64;
static int failures = 0;
static unsigned checks = 0;

static void report(bool ok, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
static void hr(HRESULT h, const char* what) {
    if (FAILED(h)) { std::printf("FAIL D3D %s 0x%08lx\n", what, static_cast<unsigned long>(h)); std::exit(1); }
}

// ---- files and the project's shader hash ------------------------------------------------------------------------------
static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
// The project's shader content hash (device_hook.cpp; billboard_facing_test.cpp uses the same function).
static uint64_t fnv1a64(const std::vector<uint8_t>& bytes) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : bytes) { h ^= b; h *= 1099511628211ull; }
    return h;
}
static std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
static unsigned countOf(const std::string& s, const std::string& part) {
    unsigned n = 0;
    for (size_t at = 0; (at = s.find(part, at)) != std::string::npos; at += part.size()) ++n;
    return n;
}

// ---- the five transcriptions and the sets they are run on -------------------------------------------------------------
struct Spec {
    const char* hash;                          // the game's name for the fixture
    const char* name;                          // the short name the registry and the header use
    const char* symbol;                        // the literal in smooth_gradients_ps.h
    const char* source;
    UINT cbRows;                               // the original's cb2[N]
    UINT samplers;                             // s0 .. s(n-1)
    std::vector<D3D_SRV_DIMENSION> textures;   // t0 .. t(n-1), by slot
    UINT seed[4];                              // the constant seed of each set (the A checks require the tone curve to be live)
};
static const Spec kSpecs[5] = {
    {"E8948389387DA083", "E894", "kSmoothGradientsE894PS", edvr::kSmoothGradientsE894PS, 16, 3,
     {D3D_SRV_DIMENSION_TEXTURE3D, D3D_SRV_DIMENSION_TEXTURE2D, D3D_SRV_DIMENSION_TEXTURE2D}, {4, 4, 4, 4}},
    {"9270C355389DA302", "9270", "kSmoothGradients9270PS", edvr::kSmoothGradients9270PS, 11, 2,
     {D3D_SRV_DIMENSION_TEXTURE2D, D3D_SRV_DIMENSION_TEXTURE2D}, {4, 4, 4, 4}},
    {"FEE777E92850B390", "FEE7", "kSmoothGradientsFEE7PS", edvr::kSmoothGradientsFEE7PS, 16, 2,
     {D3D_SRV_DIMENSION_TEXTURE3D, D3D_SRV_DIMENSION_TEXTURE2D}, {6, 4, 4, 4}},
    {"DE65BFFF2F12ECC6", "DE65", "kSmoothGradientsDE65PS", edvr::kSmoothGradientsDE65PS, 16, 2,
     {D3D_SRV_DIMENSION_TEXTURE2D, D3D_SRV_DIMENSION_TEXTURE2D}, {4, 4, 4, 4}},
    {"6E83D02E7422C5BA", "6E83", "kSmoothGradients6E83PS", edvr::kSmoothGradients6E83PS, 16, 1,
     {D3D_SRV_DIMENSION_TEXTURE2D}, {4, 4, 4, 4}},
};

struct Case {
    float w1;        // cb2[1].w: the branch flag (ge)
    float z8;        // cb2[8].z: the exposure flag (if_nz)
    float y15;       // cb2[15].y: the blackout (if_nz)
    bool bright;     // the 2D textures in 64000..65500, so the 64500 clamp binds
};
static const Case kCases[4] = {
    {0.5f, 0.0f, 0.0f, false},
    {-0.5f, 1.0f, 0.0f, false},
    {0.5f, 1.0f, 1.0f, false},
    {-0.5f, 0.0f, 0.0f, true},
};

// A fixed 32-bit LCG: state = state*1664525 + 1013904223 (mod 2^32); u = (state >> 8) / 2^24.
static std::vector<double> lcgUniform(uint32_t seed, size_t n) {
    std::vector<double> out(n);
    uint32_t st = seed;
    for (size_t i = 0; i < n; ++i) {
        st = st * 1664525u + 1013904223u;
        out[i] = static_cast<double>(st >> 8) / 16777216.0;
    }
    return out;
}
static std::vector<float> cbValues(uint32_t seed, const Case& c) {
    const std::vector<double> u = lcgUniform(seed, 64);
    std::vector<float> v(64);
    for (size_t i = 0; i < 64; ++i) v[i] = static_cast<float>(0.2 + 1.3 * u[i]);
    v[1 * 4 + 3] = c.w1;
    v[8 * 4 + 2] = c.z8;
    v[15 * 4 + 1] = c.y15;
    return v;
}
static std::vector<uint16_t> texels2d(uint32_t seed, bool bright) {
    const std::vector<double> u = lcgUniform(seed, kW * kH * 4);
    std::vector<uint16_t> h(u.size());
    for (size_t i = 0; i < u.size(); ++i) {
        const float v = bright ? static_cast<float>(64000.0 + 1500.0 * u[i]) : static_cast<float>(8.0 * u[i]);
        h[i] = XMConvertFloatToHalf(v);
    }
    return h;
}
// The 3D grade is smooth, as a game grade is: the identity ramp per axis, scaled and offset per channel by the seed. Neighbouring
// texels differ by at most kLutStep. A random grade would put up to 1.0 between neighbours, and the sub-texel weight precision of
// texture filtering (D3D11 requires 8 bits) would then turn a one-ulp difference in the grade coordinate into a step of 1/256 of
// that difference: noise of the device, not of the transcription.
constexpr double kLutStep = 1.0 / 15.0;
static std::vector<uint16_t> texels3d(uint32_t seed) {
    const std::vector<double> u = lcgUniform(seed, 6);
    std::vector<uint16_t> h(16 * 16 * 16 * 4);
    for (int z = 0; z < 16; ++z)
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const double ramp[3] = {x / 15.0, y / 15.0, z / 15.0};
                const size_t i = ((static_cast<size_t>(z) * 16 + y) * 16 + x) * 4;
                for (int c = 0; c < 3; ++c)
                    h[i + c] = XMConvertFloatToHalf(static_cast<float>(0.05 * u[c] + (0.9 + 0.05 * u[3 + c]) * ramp[c]));
                h[i + 3] = XMConvertFloatToHalf(1.0f);
            }
    return h;
}

// ---- the device --------------------------------------------------------------------------------------------------------
struct Rig {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11InfoQueue> queue;
    bool debugLayer = false;
    Rig() {
        D3D_FEATURE_LEVEL fl;
        HRESULT h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
        if (h == DXGI_ERROR_SDK_COMPONENT_MISSING)
            h = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
        else
            debugLayer = SUCCEEDED(h);
        hr(h, "CreateDevice");
        dev.As(&queue);
    }
};

// Error and corruption messages the debug layer stored during the run.
static unsigned debugErrors(ID3D11InfoQueue* q) {
    if (!q) return 0;
    unsigned n = 0;
    const UINT64 count = q->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T len = 0;
        q->GetMessage(i, nullptr, &len);
        std::vector<uint8_t> buf(len);
        D3D11_MESSAGE* msg = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
        if (SUCCEEDED(q->GetMessage(i, msg, &len)) &&
            (msg->Severity == D3D11_MESSAGE_SEVERITY_ERROR || msg->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION)) {
            ++n;
            if (n <= 3) std::printf("  debug layer: %.*s\n", static_cast<int>(msg->DescriptionByteLength), msg->pDescription);
        }
    }
    q->ClearStoredMessages();
    return n;
}

// ---- shaders -----------------------------------------------------------------------------------------------------------
// The test vertex stage: a full-screen triangle; TEXCOORD1 is (uv, z) with z ramping 0.5..4 across the screen, then SV_Position.
static const char kVsHlsl[] = R"HLSL(
struct VSOut { float3 uvz : TEXCOORD1; float4 pos : SV_Position; };
VSOut main(uint id : SV_VertexID) {
    VSOut o;
    float2 clip = float2((id == 1) ? 3.0 : -1.0, (id == 2) ? 3.0 : -1.0);
    o.pos = float4(clip, 0.0, 1.0);
    float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    o.uvz = float3(uv, 0.5 + 3.5 * uv.x);
    return o;
}
)HLSL";

// The no-macro (dither on) or EDVR_TONE_DITHER=0 compile of a transcription, as the build compiles it (entry main, flags 0).
static std::vector<uint8_t> compileHlsl(const char* text, const char* target, bool dither) {
    D3D_SHADER_MACRO macros[] = {{"EDVR_TONE_DITHER", "0"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> code, err;
    const HRESULT h = D3DCompile(text, std::strlen(text), "smooth_gradients", dither ? nullptr : macros, nullptr, "main", target, 0, 0,
                                 &code, &err);
    if (FAILED(h)) {
        std::printf("FAIL compile %s: %s\n", target, err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        std::exit(1);
    }
    const uint8_t* p = static_cast<const uint8_t*>(code->GetBufferPointer());
    return std::vector<uint8_t>(p, p + code->GetBufferSize());
}
static ComPtr<ID3D11PixelShader> makePs(ID3D11Device* dev, const std::vector<uint8_t>& bytes) {
    ComPtr<ID3D11PixelShader> ps;
    hr(dev->CreatePixelShader(bytes.data(), bytes.size(), nullptr, &ps), "CreatePixelShader");
    return ps;
}

// ---- bindings and one draw ---------------------------------------------------------------------------------------------
struct Bindings {
    ComPtr<ID3D11Buffer> cb;                                // cb2, bound at b2
    std::vector<ComPtr<ID3D11ShaderResourceView>> srv;      // t0 .. by slot
    ComPtr<ID3D11SamplerState> samp;                        // linear clamp, at every declared sampler slot
    UINT samplers = 0;
};

static ComPtr<ID3D11ShaderResourceView> makeTexture(ID3D11Device* dev, D3D_SRV_DIMENSION dim, const std::vector<uint16_t>& texels) {
    ComPtr<ID3D11ShaderResourceView> srv;
    if (dim == D3D_SRV_DIMENSION_TEXTURE3D) {
        D3D11_TEXTURE3D_DESC d{};
        d.Width = 16;
        d.Height = 16;
        d.Depth = 16;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = texels.data();
        sd.SysMemPitch = 16 * 8;
        sd.SysMemSlicePitch = 16 * 16 * 8;
        ComPtr<ID3D11Texture3D> tex;
        hr(dev->CreateTexture3D(&d, &sd, &tex), "CreateTexture3D");
        hr(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv), "CreateSRV 3D");
    } else {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = kW;
        d.Height = kH;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = texels.data();
        sd.SysMemPitch = kW * 8;
        ComPtr<ID3D11Texture2D> tex;
        hr(dev->CreateTexture2D(&d, &sd, &tex), "CreateTexture2D");
        hr(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv), "CreateSRV 2D");
    }
    return srv;
}

// The inputs of one run: the case's constants and textures, for one shader.
static Bindings makeBindings(ID3D11Device* dev, const Spec& sp, int k) {
    const Case& c = kCases[k];
    Bindings b;
    const std::vector<float> cb = cbValues(sp.seed[k], c);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sp.cbRows * 16;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cbData{};
    cbData.pSysMem = cb.data();
    hr(dev->CreateBuffer(&bd, &cbData, &b.cb), "CreateBuffer cb2");
    for (size_t s = 0; s < sp.textures.size(); ++s) {
        const uint32_t seed = 5000u + 10u * static_cast<uint32_t>(k) + static_cast<uint32_t>(s);
        const std::vector<uint16_t> texels = sp.textures[s] == D3D_SRV_DIMENSION_TEXTURE3D ? texels3d(seed) : texels2d(seed, c.bright);
        b.srv.push_back(makeTexture(dev, sp.textures[s], texels));
    }
    D3D11_SAMPLER_DESC sdsc{};
    sdsc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sdsc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdsc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdsc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sdsc.MaxAnisotropy = 1;
    sdsc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sdsc.MaxLOD = D3D11_FLOAT32_MAX;
    hr(dev->CreateSamplerState(&sdsc, &b.samp), "CreateSamplerState");
    b.samplers = sp.samplers;
    return b;
}

// One full-screen draw into a 64x64 target of the given format; returns the target's rows, tightly packed.
static std::vector<uint8_t> renderRaw(Rig& rig, ID3D11VertexShader* vs, ID3D11PixelShader* ps, const Bindings& b, DXGI_FORMAT fmt, UINT bpp) {
    ID3D11Device* dev = rig.dev.Get();
    ID3D11DeviceContext* ctx = rig.ctx.Get();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kW;
    td.Height = kH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> rt;
    hr(dev->CreateTexture2D(&td, nullptr, &rt), "CreateTexture2D target");
    ComPtr<ID3D11RenderTargetView> rtv;
    hr(dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv), "CreateRenderTargetView");

    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(kW), static_cast<float>(kH), 0.0f, 1.0f};
    ID3D11RenderTargetView* views[1] = {rtv.Get()};
    ctx->OMSetRenderTargets(1, views, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // No culling: the full-screen triangle is counter-clockwise in NDC, which the default (back-face culled) state would drop, and the
    // target would then stay black on both sides, which is why the live-curve check exists.
    D3D11_RASTERIZER_DESC rsd{};
    rsd.FillMode = D3D11_FILL_SOLID;
    rsd.CullMode = D3D11_CULL_NONE;
    rsd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rs;
    hr(dev->CreateRasterizerState(&rsd, &rs), "CreateRasterizerState");
    ctx->RSSetState(rs.Get());
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ID3D11Buffer* cbs[1] = {b.cb.Get()};
    ctx->PSSetConstantBuffers(2, 1, cbs);
    for (UINT s = 0; s < b.srv.size(); ++s) {
        ID3D11ShaderResourceView* srv = b.srv[s].Get();
        ctx->PSSetShaderResources(s, 1, &srv);
    }
    ID3D11SamplerState* samp = b.samp.Get();
    for (UINT s = 0; s < b.samplers; ++s) ctx->PSSetSamplers(s, 1, &samp);
    ctx->Draw(3, 0);

    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    hr(dev->CreateTexture2D(&td, nullptr, &staging), "CreateTexture2D staging");
    ctx->CopyResource(staging.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    hr(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m), "Map staging");
    std::vector<uint8_t> out(static_cast<size_t>(kW) * kH * bpp);
    for (UINT y = 0; y < kH; ++y)
        std::memcpy(&out[static_cast<size_t>(y) * kW * bpp], static_cast<const uint8_t*>(m.pData) + static_cast<size_t>(y) * m.RowPitch, kW * bpp);
    ctx->Unmap(staging.Get(), 0);
    return out;
}
static std::vector<float> asFloats(const std::vector<uint8_t>& raw) {
    std::vector<float> f(raw.size() / 4);
    std::memcpy(f.data(), raw.data(), f.size() * 4);
    return f;
}
static float clamp01(float x) {
    if (std::isnan(x)) return 0.0f;
    return std::min(1.0f, std::max(0.0f, x));
}

// ---- the checks ----------------------------------------------------------------------------------------------------------
static void testEquivalence(Rig& rig, ID3D11VertexShader* vs, const Spec& sp, ID3D11PixelShader* psOrig, ID3D11PixelShader* psRef) {
    for (int k = 0; k < 4; ++k) {
        const Bindings b = makeBindings(rig.dev.Get(), sp, k);
        const std::vector<float> a = asFloats(renderRaw(rig, vs, psOrig, b, DXGI_FORMAT_R32G32B32A32_FLOAT, 16));
        const std::vector<float> o = asFloats(renderRaw(rig, vs, psRef, b, DXGI_FORMAT_R32G32B32A32_FLOAT, 16));
        size_t finite = 0, bad = 0, live = 0;
        double worst = 0.0;
        for (size_t p = 0; p < static_cast<size_t>(kW) * kH; ++p) {
            bool pixelLive = false;
            for (int ch = 0; ch < 4; ++ch) {
                const float x = a[p * 4 + ch], y = o[p * 4 + ch];
                if (std::isfinite(x)) ++finite;
                if (ch < 3 && x > 1e-3f && x < 0.999f) pixelLive = true;
                const bool same = (std::isnan(x) && std::isnan(y)) || x == y ||
                                  std::fabs(static_cast<double>(x) - static_cast<double>(y)) <= 1e-4 * std::max(1.0, std::fabs(static_cast<double>(x)));
                if (!same) ++bad;
                if (std::isfinite(x) && std::isfinite(y)) worst = std::max(worst, std::fabs(static_cast<double>(x) - static_cast<double>(y)));
            }
            if (pixelLive) ++live;
        }
        const double total = static_cast<double>(kW) * kH * 4;
        const double finiteFraction = static_cast<double>(finite) / total;
        const double liveFraction = static_cast<double>(live) / (kW * kH);
        char label[160];
        std::snprintf(label, sizeof label, "A %s set%d equivalence: %zu mismatched channels, worst |a-b| %.3g, finite %.3f", sp.name, k, bad, worst, finiteFraction);
        report(bad == 0 && finiteFraction >= 0.90, label);
        if (k != 2) {
            std::snprintf(label, sizeof label, "A %s set%d live tone curve: %.3f of pixels with RGB inside (0.001, 0.999)", sp.name, k, liveFraction);
            report(liveFraction >= 0.25, label);
        }
    }
}

static void testDither(Rig& rig, ID3D11VertexShader* vs, const Spec& sp, ID3D11PixelShader* psRef, ID3D11PixelShader* psProd) {
    for (int k : {0, 2}) {
        const Bindings b = makeBindings(rig.dev.Get(), sp, k);
        const std::vector<float> ref = asFloats(renderRaw(rig, vs, psRef, b, DXGI_FORMAT_R32G32B32A32_FLOAT, 16));
        const std::vector<uint8_t> q = renderRaw(rig, vs, psProd, b, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
        const std::vector<uint8_t> qRef = renderRaw(rig, vs, psRef, b, DXGI_FORMAT_R8G8B8A8_UNORM, 4);
        bool bound = true, zero = true, alpha = true, alphaUntouched = true;
        size_t alphaBad = 0, firstBad = 0;
        double worstBound = 0.0, worstBlock = 0.0;
        for (size_t p = 0; p < static_cast<size_t>(kW) * kH; ++p) {
            for (int ch = 0; ch < 3; ++ch) {
                const double want = static_cast<double>(clamp01(ref[p * 4 + ch])) * 255.0;
                const double got = q[p * 4 + ch];
                worstBound = std::max(worstBound, std::fabs(got - want));
                if (std::fabs(got - want) > 1.6) bound = false;
                if (ref[p * 4 + ch] == 0.0f && q[p * 4 + ch] != 0) zero = false;
            }
            const double wantA = static_cast<double>(clamp01(ref[p * 4 + 3])) * 255.0;
            if (std::fabs(static_cast<double>(q[p * 4 + 3]) - wantA) > 0.6) {
                if (alphaBad == 0) firstBad = p;
                ++alphaBad;
                alpha = false;
            }
            if (q[p * 4 + 3] != qRef[p * 4 + 3]) alphaUntouched = false;
        }
        for (UINT by = 0; by < kH; by += 16)
            for (UINT bx = 0; bx < kW; bx += 16)
                for (int ch = 0; ch < 3; ++ch) {
                    double sq = 0.0, sw = 0.0;
                    for (UINT y = by; y < by + 16; ++y)
                        for (UINT x = bx; x < bx + 16; ++x) {
                            const size_t p = static_cast<size_t>(y) * kW + x;
                            sq += q[p * 4 + ch];
                            sw += static_cast<double>(clamp01(ref[p * 4 + ch])) * 255.0;
                        }
                    worstBlock = std::max(worstBlock, std::fabs(sq - sw) / 256.0);
                }
        char label[160];
        if (alphaBad) {
            const size_t p = firstBad;
            std::printf("INFO B %s set%d alpha beyond 0.6 of ref.a*255: %zu of %u pixels; first p=%zu ref.a=%.9g q.a=%u no-dither q.a=%u ref.a*255=%.3f\n",
                        sp.name, k, alphaBad, kW * kH, p, ref[p * 4 + 3], static_cast<unsigned>(q[p * 4 + 3]),
                        static_cast<unsigned>(qRef[p * 4 + 3]), static_cast<double>(clamp01(ref[p * 4 + 3])) * 255.0);
        }
        std::snprintf(label, sizeof label, "B %s set%d alpha untouched by the dither (8-bit, as the no-dither transcription writes it)", sp.name, k);
        report(alphaUntouched, label);
        std::snprintf(label, sizeof label, "B %s set%d dither bound: worst |q - ref*255| %.3f", sp.name, k, worstBound);
        report(bound, label);
        std::snprintf(label, sizeof label, "B %s set%d 16x16 block means: worst %.3f", sp.name, k, worstBlock);
        report(worstBlock <= 0.5, label);
        std::snprintf(label, sizeof label, "B %s set%d exact zero stays zero", sp.name, k);
        report(zero, label);
        std::snprintf(label, sizeof label, "B %s set%d alpha within the conversion tolerance (0.6) of ref.a*255", sp.name, k);
        report(alpha, label);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string mode = argv[1];
    if (mode == "--dry-run") {
        std::puts("smooth_gradients_test: F1, A, B, S; WARP with the debug layer; reads the fixtures and sources, writes nothing");
        return 0;
    }
    if (mode != "--self-test" || argc < 3) return 2;
    const std::string root = argv[2];
    Rig rig;
    std::printf("smooth_gradients_test: debug layer %s\n", rig.debugLayer ? "ON" : "not installed (messages not checked)");

    const std::vector<uint8_t> vsBytes = compileHlsl(kVsHlsl, "vs_5_0", true);
    ComPtr<ID3D11VertexShader> vs;
    hr(rig.dev->CreateVertexShader(vsBytes.data(), vsBytes.size(), nullptr, &vs), "CreateVertexShader");

    for (const Spec& sp : kSpecs) {
        const std::vector<uint8_t> original = readFile(root + "\\tools\\smooth_gradients_test\\fixtures\\ps_" + sp.hash + ".dxbc");
        char nameBuf[32];
        std::snprintf(nameBuf, sizeof nameBuf, "%016llX", static_cast<unsigned long long>(fnv1a64(original)));
        report(!original.empty() && std::strcmp(nameBuf, sp.hash) == 0, std::string("F1 fixture ") + sp.name + " hashes to its name");
        if (original.empty()) continue;
        const ComPtr<ID3D11PixelShader> psOrig = makePs(rig.dev.Get(), original);
        const ComPtr<ID3D11PixelShader> psRef = makePs(rig.dev.Get(), compileHlsl(sp.source, "ps_5_0", false));
        const ComPtr<ID3D11PixelShader> psProd = makePs(rig.dev.Get(), compileHlsl(sp.source, "ps_5_0", true));
        testEquivalence(rig, vs.Get(), sp, psOrig.Get(), psRef.Get());
        testDither(rig, vs.Get(), sp, psRef.Get(), psProd.Get());
    }

    // S: the registry and the header name the five.
    {
        const std::string header = slurp(root + "\\src\\d3d11\\smooth_gradients_ps.h");
        const std::string registry = slurp(root + "\\tools\\temporal_shader_build\\fixed_smooth_gradients_variants.h");
        for (const Spec& sp : kSpecs) {
            report(countOf(header, std::string("constexpr char ") + sp.symbol + "[]") == 1, std::string("S header declares ") + sp.symbol);
            report(countOf(registry, sp.symbol) == 1, std::string("S registry builds ") + sp.name + " from the header's literal");
        }
    }

    if (rig.debugLayer) report(debugErrors(rig.queue.Get()) == 0, "debug layer: no error or corruption message during the run");
    if (failures) {
        std::printf("smooth_gradients_test: %d FAILED of %u\n", failures, checks);
        return 1;
    }
    std::printf("smooth_gradients_test: all %u checks passed\n", checks);
    return 0;
}
