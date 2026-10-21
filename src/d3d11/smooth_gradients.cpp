#include "temporal_shader_bytecode.h"
#include "smooth_gradients.h"

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <string>

#include "binding_shadow.h"
#include "shader_swap.h"
#include "vscreen.h"
#include "../common/config.h"
#include "../common/guard.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"

namespace edvr {
namespace {

// The five transcriptions (smooth_gradients_ps.h) and the game shader each one replaces, by its hash. One table: the hash,
// the compiled bytes, and the name the driver and the log know the replacement by.
struct ToneVariant {
    uint64_t gameHash;
    const void* bytecode;
    size_t bytecodeLen;
    const char* name;
    ID3D11PixelShader* shader;   // made on first use, on the device the cache belongs to
    bool failed;                 // creation failed this session: this tone pass stays stock
    bool announced;              // its first dither has been logged
};

ToneVariant g_tones[] = {
    {0xE8948389387DA083ull, kSmoothGradientsE894Bytecode, sizeof(kSmoothGradientsE894Bytecode), "smooth_gradients_E894_ps", nullptr, false, false},
    {0x9270C355389DA302ull, kSmoothGradients9270Bytecode, sizeof(kSmoothGradients9270Bytecode), "smooth_gradients_9270_ps", nullptr, false, false},
    {0xFEE777E92850B390ull, kSmoothGradientsFEE7Bytecode, sizeof(kSmoothGradientsFEE7Bytecode), "smooth_gradients_FEE7_ps", nullptr, false, false},
    {0xDE65BFFF2F12ECC6ull, kSmoothGradientsDE65Bytecode, sizeof(kSmoothGradientsDE65Bytecode), "smooth_gradients_DE65_ps", nullptr, false, false},
    {0x6E83D02E7422C5BAull, kSmoothGradients6E83Bytecode, sizeof(kSmoothGradients6E83Bytecode), "smooth_gradients_6E83_ps", nullptr, false, false},
};

// What configure last reported, so each change of state is logged once: off, the flat edition on, or VR with the key on.
constexpr int kReportOff = 0;
constexpr int kReportFlat = 1;
constexpr int kReportVr = 2;

FaultBudget g_budget("smoothGradients", 4);

std::atomic<bool> g_enabled{false};
std::atomic<int> g_reported{-1};

// The device the replacements were made on. Held, so another device can never be mistaken for it at the same address.
ID3D11Device* g_device = nullptr;

// Between a Begin that bound the replacement and its End: the game's shader, which End puts back.
ID3D11PixelShader* g_savedPs = nullptr;
bool g_engaged = false;

struct Counts {
    uint32_t dithered = 0;
    uint32_t targetNot8Bit = 0;
    uint32_t replacementUnavailable = 0;
    uint32_t otherFullScreen = 0;
};
Counts g_counts;

// The other full-screen 8-bit draws, by game pixel shader: a list of four. A hash that finds the list full takes the least-counted
// entry, so a count in it is an upper bound for a shader that came in late.
struct OtherShader {
    uint64_t hash = 0;
    uint32_t count = 0;
};
OtherShader g_other[4];

uint64_t g_lastSummaryMs = 0;

ToneVariant* findTone(uint64_t hash) {
    for (ToneVariant& t : g_tones) {
        if (t.gameHash == hash) return &t;
    }
    return nullptr;
}

void releaseReplacements() {
    for (ToneVariant& t : g_tones) {
        if (t.shader) {
            t.shader->Release();
            t.shader = nullptr;
        }
    }
}

// The replacements are made on one device. When the game makes a new one, the old replacements go and the next tone draw makes
// them again on the new device.
bool adoptDevice(ID3D11DeviceContext* ctx) {
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return false;
    if (dev == g_device) {
        dev->Release();
        return true;
    }
    releaseReplacements();
    if (g_device) g_device->Release();
    g_device = dev;   // the reference GetDevice gave
    return true;
}

void noteOtherShader(uint64_t hash) {
    ++g_counts.otherFullScreen;
    OtherShader* slot = nullptr;
    for (OtherShader& e : g_other) {
        if (e.count && e.hash == hash) {
            ++e.count;
            return;
        }
        if (!slot || e.count < slot->count) slot = &e;
    }
    slot->hash = hash;
    ++slot->count;
}

// The 30 s line. Called from every Begin that gets as far as the render target, so the timer needs no thread of its own.
void noteSummaryIfDue() {
    const uint64_t now = GetTickCount64();
    if (g_lastSummaryMs == 0) {
        g_lastSummaryMs = now;
        return;
    }
    if (now - g_lastSummaryMs < 30000) return;
    g_lastSummaryMs = now;
    const Counts c = g_counts;
    if (!c.dithered && !c.targetNot8Bit && !c.replacementUnavailable && !c.otherFullScreen) return;

    OtherShader top[4];
    std::copy(std::begin(g_other), std::end(g_other), top);
    std::sort(top, top + 4, [](const OtherShader& a, const OtherShader& b) { return a.count > b.count; });
    std::string list;
    for (const OtherShader& e : top) {
        if (!e.count) continue;
        char part[48];
        std::snprintf(part, sizeof(part), "%s%016llX x%u", list.empty() ? "" : ", ",
                      static_cast<unsigned long long>(e.hash), e.count);
        list += part;
    }
    Log::get().note("smooth gradients 30s: tone draws dithered %u; left stock: target not 8-bit UNORM %u, replacement unavailable %u; "
                    "other full-screen 8-bit draws %u (top pixel shaders: %s)",
                    c.dithered, c.targetNot8Bit, c.replacementUnavailable, c.otherFullScreen, list.empty() ? "none" : list.c_str());
    g_counts = Counts{};
    std::fill(std::begin(g_other), std::end(g_other), OtherShader{});
}

}  // namespace

void smoothGradientsConfigure(Config& cfg) {
    const bool keyOn = cfg.getBool("fix.smooth_gradients", true);
    const bool flat = runtimeFlatProfile();
    const bool on = flat && keyOn;
    g_enabled.store(on, std::memory_order_relaxed);

    const int report = on ? kReportFlat : (keyOn && runtimeVrProfile()) ? kReportVr : kReportOff;
    if (g_reported.exchange(report) == report) return;
    if (report == kReportFlat) {
        Log::get().note("smooth gradients: on (fix.smooth_gradients) -- the flat tone pass's 8-bit write gets a one-code dither, on the "
                        "five tone shaders transcribed (E8948389, 9270C355, FEE777E9, DE65BFFF, 6E83D02E); any other tone shader draws stock");
    } else if (report == kReportVr) {
        Log::get().note("smooth gradients: not in VR yet -- its tone shader has not been transcribed, so VR draws stock whatever "
                        "fix.smooth_gradients says");
    } else {
        Log::get().note("smooth gradients: off (fix.smooth_gradients) -- the tone passes draw stock");
    }
}

bool smoothGradientsBegin(ID3D11DeviceContext* ctx, char kind, uint32_t count, uint32_t instances, uint32_t startInstance) {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    if ((kind != 'D' && kind != 'N') || count != 3 || instances != 1 || startInstance != 0) return false;
    if (!ctx || g_engaged) return false;

    const uint64_t ps = bindingShaderHash(BindSlot::Ps);
    ID3D11RenderTargetView* rawRtv = nullptr;
    ctx->OMGetRenderTargets(1, &rawRtv, nullptr);
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    rtv.Attach(rawRtv);
    noteSummaryIfDue();
    if (!rtv) return false;

    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rtv->GetDesc(&rd);
    const bool eightBit = rd.Format == DXGI_FORMAT_R8G8B8A8_UNORM || rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM;

    ToneVariant* tone = findTone(ps);
    if (!tone) {
        if (eightBit) noteOtherShader(ps);
        return false;
    }
    if (!eightBit) {
        ++g_counts.targetNot8Bit;
        return false;
    }
    if (tone->failed) {
        ++g_counts.replacementUnavailable;
        return false;
    }
    if (!adoptDevice(ctx)) return false;
    if (!tone->shader) {
        tone->shader = shaderSwapCreatePs(ctx, tone->bytecode, tone->bytecodeLen, tone->name, "smooth gradients");
        if (!tone->shader) {
            tone->failed = true;
            ++g_counts.replacementUnavailable;
            Log::get().note("smooth gradients: the replacement for tone pixel shader %016llX could not be created, so that tone pass "
                            "stays stock this session", static_cast<unsigned long long>(ps));
            return false;
        }
    }

    // Read the game's shader first. The tone shaders use no class linkage, so a game shader that does is left stock.
    ID3D11PixelShader* saved = nullptr;
    bool plain = false;
    guardedBudget(g_budget, [&] {
        ID3D11ClassInstance* classes[8] = {};
        UINT classCount = 8;
        ctx->PSGetShader(&saved, classes, &classCount);
        for (UINT i = 0; i < classCount && i < 8; ++i) {
            if (classes[i]) classes[i]->Release();
        }
        plain = classCount == 0 && saved != nullptr;
    });
    if (!plain) {
        if (saved) saved->Release();
        return false;
    }

    // From here the saved reference belongs to End, which restores the game's shader whether or not the bind below went
    // through: once g_engaged is set, Begin has to return true so that End is called.
    g_savedPs = saved;
    g_engaged = true;
    const bool bound = guardedBudget(g_budget, [&] { vScreenPSSetShaderRaw(ctx, tone->shader, nullptr, 0); });
    if (!bound) return true;
    ++g_counts.dithered;

    if (!tone->announced) {
        tone->announced = true;
        uint32_t w = 0, h = 0;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        rtv->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (resource && SUCCEEDED(resource.As(&texture))) {
            D3D11_TEXTURE2D_DESC td{};
            texture->GetDesc(&td);
            w = td.Width;
            h = td.Height;
        }
        Log::get().note("smooth gradients: dithering tone pixel shader %016llX (target %ux%u, view format %u)",
                        static_cast<unsigned long long>(ps), w, h, static_cast<unsigned>(rd.Format));
    }
    return true;
}

void smoothGradientsEnd(ID3D11DeviceContext* ctx) {
    if (!ctx || !g_engaged) return;
    g_engaged = false;
    guardedBudget(g_budget, [&] { vScreenPSSetShaderRaw(ctx, g_savedPs, nullptr, 0); });
    if (g_savedPs) {
        g_savedPs->Release();
        g_savedPs = nullptr;
    }
}

void smoothGradientsShutdown() {
    g_enabled.store(false, std::memory_order_relaxed);
    g_engaged = false;
    if (g_savedPs) {
        g_savedPs->Release();
        g_savedPs = nullptr;
    }
    releaseReplacements();
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
}

}  // namespace edvr
