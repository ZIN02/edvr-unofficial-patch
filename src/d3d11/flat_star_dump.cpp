#include "flat_star_dump.h"

#include <windows.h>

#include <d3d11.h>

#include "../common/guard.h"
#include "../common/log.h"
#include "../common/runtime_profile.h"
#include "binding_shadow.h"
#include "flat_runtime.h"
#include "journal_watch.h"

// The star dump's runtime half (flat_star_dump.h says what it is for and what each outcome looks like in the log). Observe-only: nothing
// here binds, sets, copies or reads back on the GPU, and nothing a consumer decides depends on it.
namespace edvr {
namespace {

stardump::Census g_census;
stardump::SeenSet g_seen;
bool g_active = false;   // this census's begin line was written, in the flat profile
FaultBudget g_budget("flatStarDump", 3);

// The journal's account of the star, printed in the census header: the latest hyperspace StartJump's destination and class, and the stars
// scanned in the current system. "truth none" means the journal watcher has read neither (a session that has not jumped, a watcher that is
// off or has not yet reached the events): it is not a star class.
void noteTruth(uint32_t census) {
    journalstar::Table t;
    if (!journalStarSnapshot(&t)) {
        Log::get().note("flat star dump: truth none census=%u -- no hyperspace StartJump, FSDJump or star Scan has been read from the journal "
                        "this session (jumps=%u arrivals=%u), so the dump below has no ground truth", census, t.jumps, t.arrivals);
        return;
    }
    char text[512];
    if (t.jump.valid) {
        journalstar::formatJump(text, sizeof(text), t.jump);
        Log::get().note("flat star dump: truth jump census=%u %s in-system=\"%s\" jumps=%u arrivals=%u", census, text,
                        t.arrivedSystem[0] ? t.arrivedSystem : "-", t.jumps, t.arrivals);
    } else {
        Log::get().note("flat star dump: truth jump census=%u none -- no hyperspace StartJump read; in-system=\"%s\" arrivals=%u", census,
                        t.arrivedSystem[0] ? t.arrivedSystem : "-", t.arrivals);
    }
    for (uint32_t i = 0; i < t.count; ++i) {
        journalstar::formatStar(text, sizeof(text), t.stars[i]);
        Log::get().note("flat star dump: truth star %u/%u census=%u %s", i + 1, t.count, census, text);
    }
    if (!t.count)
        Log::get().note("flat star dump: truth stars census=%u none scanned in this system yet (repeats=%u dropped=%u)", census, t.repeats,
                        t.dropped);
    else if (t.dropped)
        Log::get().note("flat star dump: truth stars census=%u %u more than the table holds were not kept", census, t.dropped);
}

void dumpBuffer(const StarDrawFacts& f, bool pixel, uint32_t slot, const FlatBoundConstants& info, const unsigned char* bytes) {
    const char* stage = pixel ? "PS" : "VS";
    ++g_census.buffers;
    if (!info.copied) {
        ++g_census.unshadowed;
        Log::get().note("flat star cb: census=%u q=%u %s b%u buf=%p width=%u off=%u win=%u unshadowed=%s tracked=%d wr=%llu now=%llu",
                        g_census.number, f.q, stage, slot, info.buffer, info.width, info.firstByte, info.windowBytes, info.why,
                        info.tracked ? 1 : 0, static_cast<unsigned long long>(info.writeFrame),
                        static_cast<unsigned long long>(info.frame));
        return;
    }
    ++g_census.shadowed;
    const uint64_t hash = fnv1a64(bytes, info.copied);
    const stardump::Seen* same = g_seen.find(hash, info.copied);
    if (same) {
        ++g_census.duplicate;
        Log::get().note("flat star cb: census=%u q=%u %s b%u buf=%p width=%u off=%u win=%u dump=%u tracked=%d wr=%llu now=%llu h=%016llX "
                        "same-bytes-as q=%u %s b%u",
                        g_census.number, f.q, stage, slot, info.buffer, info.width, info.firstByte, info.windowBytes, info.copied,
                        info.tracked ? 1 : 0, static_cast<unsigned long long>(info.writeFrame),
                        static_cast<unsigned long long>(info.frame), static_cast<unsigned long long>(hash), same->q, same->pixel ? "PS" : "VS",
                        same->slot);
        return;
    }
    g_seen.add(hash, info.copied, f.q, slot, pixel);
    Log::get().note("flat star cb: census=%u q=%u %s b%u buf=%p width=%u off=%u win=%u dump=%u tracked=%d wr=%llu now=%llu h=%016llX "
                    "data=below",
                    g_census.number, f.q, stage, slot, info.buffer, info.width, info.firstByte, info.windowBytes, info.copied,
                    info.tracked ? 1 : 0, static_cast<unsigned long long>(info.writeFrame), static_cast<unsigned long long>(info.frame),
                    static_cast<unsigned long long>(hash));
    for (uint32_t off = 0; off < info.copied; off += stardump::kLineBytes) {
        const uint32_t n = info.copied - off < stardump::kLineBytes ? info.copied - off : stardump::kLineBytes;
        char hex[300];
        char fl[440];
        if (!stardump::formatWords(hex, sizeof(hex), fl, sizeof(fl), bytes + off, n)) break;
        Log::get().note("flat star cb data: census=%u q=%u %s b%u +%u hex=%s f=%s", g_census.number, f.q, stage, slot, off, hex, fl);
    }
}

void dumpDraw(ID3D11DeviceContext* ctx, const StarDrawFacts& f) {
    // The draw's state, from the context: the topology, the blend of render target 0 (a null state is the API default: opaque, all channels),
    // and the render target's size and format off the binding shadow's resolver.
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ctx->IAGetPrimitiveTopology(&topo);
    ID3D11BlendState* bs = nullptr;
    FLOAT factor[4] = {};
    UINT mask = 0;
    ctx->OMGetBlendState(&bs, factor, &mask);
    int blend = 0;
    unsigned writeMask = 0xFu;
    if (bs) {
        D3D11_BLEND_DESC bd{};
        bs->GetDesc(&bd);
        blend = bd.RenderTarget[0].BlendEnable ? 1 : 0;
        writeMask = bd.RenderTarget[0].RenderTargetWriteMask;
        bs->Release();
    }
    ResourceInfo rt;
    const bool haveRt = bindingGet(BindSlot::Rtv0) && bindingResolveProbe(bindingGet(BindSlot::Rtv0), &rt) && rt.isTexture2D;
    const int dsv = bindingGet(BindSlot::Dsv0) ? 1 : 0;

    // Which slots hold a buffer, one call a stage; the contents are read slot by slot below.
    ID3D11Buffer* vsb[stardump::kSlots] = {};
    ID3D11Buffer* psb[stardump::kSlots] = {};
    ctx->VSGetConstantBuffers(0, stardump::kSlots, vsb);
    ctx->PSGetConstantBuffers(0, stardump::kSlots, psb);
    char vsList[24], psList[24];
    stardump::slotList(reinterpret_cast<const void* const*>(vsb), stardump::kSlots, vsList, sizeof(vsList));
    stardump::slotList(reinterpret_cast<const void* const*>(psb), stardump::kSlots, psList, sizeof(psList));

    Log::get().note("flat star draw: census=%u frame=%u %s #%u q=%u kind=%c n=%u i=%u start=%u base=%d vs=%016llX ps=%016llX vs-listed=%d "
                    "ps-listed=%d topo=%u rt=%s %ux%u fmt=%u dsv=%s bound=%d bl=%d wm=%X vs-cb=%s ps-cb=%s",
                    g_census.number, f.frame, f.tag, f.index, f.q, f.kind, f.count, f.instances, f.start, static_cast<int>(f.base),
                    static_cast<unsigned long long>(f.vs), static_cast<unsigned long long>(f.ps), stardump::solarVertex(f.vs) ? 1 : 0,
                    stardump::solarPixel(f.ps) ? 1 : 0, static_cast<unsigned>(topo), f.rtv, haveRt ? rt.a : 0u, haveRt ? rt.b : 0u,
                    haveRt ? rt.fmt : 0u, f.dsv, dsv, blend, writeMask, vsList, psList);

    unsigned char bytes[stardump::kBufferCap];
    for (int stage = 0; stage < 2; ++stage) {
        const bool pixel = stage == 1;
        const ID3D11Buffer* const* bound = pixel ? psb : vsb;
        for (uint32_t slot = 0; slot < stardump::kSlots; ++slot) {
            if (!bound[slot]) continue;
            FlatBoundConstants info;
            flatRuntimeObserveConstants(ctx, pixel, slot, stardump::kBufferCap, bytes, &info);
            if (!info.bound) {
                // The accessor refused before it reached the buffer (the reason is in info.why): the slot still holds one, and its size is
                // asked here, so a bound buffer is never silently left out of the dump.
                D3D11_BUFFER_DESC bd{};
                const_cast<ID3D11Buffer*>(bound[slot])->GetDesc(&bd);
                info.buffer = bound[slot];
                info.width = bd.ByteWidth;
                info.windowBytes = bd.ByteWidth;
            }
            dumpBuffer(f, pixel, slot, info, bytes);
        }
    }
    for (ID3D11Buffer* b : vsb) if (b) b->Release();
    for (ID3D11Buffer* b : psb) if (b) b->Release();
}

}  // namespace

void flatStarDumpFirstSight(uint64_t vs, uint64_t ps, char kind, uint32_t count, uint32_t instances) {
    if (!runtimeFlatProfile()) return;
    if (stardetail::g_firstSightDone.exchange(true, std::memory_order_relaxed)) return;
    Log::get().note("flat star first sight: vs=%016llX ps=%016llX vs-listed=%d ps-listed=%d kind=%c n=%u i=%u -- the first draw this "
                    "session whose VS is in the solar list or whose PS is in the solar-pixel list; a census taken with a star on screen "
                    "that shows none of these means the flat star draws are not in the lists",
                    static_cast<unsigned long long>(vs), static_cast<unsigned long long>(ps), stardump::solarVertex(vs) ? 1 : 0,
                    stardump::solarPixel(ps) ? 1 : 0, kind, count, instances);
}

void flatStarDumpBegin(uint32_t census, uint32_t frames, uint32_t frameNo) {
    g_active = false;
    if (!runtimeFlatProfile()) return;
    g_active = true;
    g_census = stardump::Census();
    g_census.number = census;
    g_seen.clear();
    Log::get().note("flat star dump: begin census=%u frames=%u frame=%u -- draws whose VS is in the solar list or whose PS is in the solar-pixel "
                    "list are dumped with VS and PS constant buffers b0-b7 (%u bytes each at most), %u draws at most; observe-only",
                    census, frames, frameNo, stardump::kBufferCap, stardump::kMaxDraws);
    noteTruth(census);
}

void flatStarDumpCapMissed() {
    if (g_active) ++g_census.capMissed;
}

void flatStarDumpDraw(ID3D11DeviceContext* ctx, const StarDrawFacts& f) {
    if (!g_active || !ctx) return;
    if (!g_census.take()) return;
    if (!guardedBudget(g_budget, [&] { dumpDraw(ctx, f); })) ++g_census.faults;
}

void flatStarDumpEnd(uint32_t census) {
    if (!g_active) return;
    g_active = false;
    Log::get().note("flat star dump: end census=%u star-draws=%u dumped=%u skipped=%u cap-missed=%u faults=%u buffers=%u shadowed=%u "
                    "unshadowed=%u same-bytes=%u first-sight=%d",
                    census, g_census.seen, g_census.dumped, g_census.skipped, g_census.capMissed, g_census.faults, g_census.buffers,
                    g_census.shadowed, g_census.unshadowed, g_census.duplicate,
                    stardetail::g_firstSightDone.load(std::memory_order_relaxed) ? 1 : 0);
}

}  // namespace edvr
