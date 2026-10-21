#pragma once
// The source-free camera witness (src/d3d11/flat_runtime_model.h: FlatRuntimeCameraWitness, flatRuntimeNoteWitness, flatRuntimeDeferredDonor).
// A scene whose every G-buffer writer is an unkeyed or VR-only pair (the loadout screen's character preview) has no source record, so the
// deferred lighting pass found no donor and the frame alternated between treated and refused. The resolver now falls back to the camera
// the Screen-kind writers of the pass's SRV0 bound, only where no source record of any kind exists on the depth, and only when nothing
// else on that depth carries another camera. Every proof bound downstream is untouched, so these tests judge the resolver and the three
// places that ask it, not the camera match.
#include "flat_laser_scene_tone_tests.h"
#include "../../src/d3d11/flat_deferred_failure_witness.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace deferred_witness_test {
using namespace edvr;
using laser_scene_tone_test::token;
using laser_scene_tone_test::draw;
using laser_scene_tone_test::camera;

constexpr uint64_t kFrame = 36960;
constexpr uint64_t kPoolVs = 0xBFE51414CC3024B4ull, kPoolPs = 0xDB79AE788E049DFDull;   // a keyed pool pair
constexpr uint64_t kPreviewPs = 0xA17504A2627767F2ull;                                 // the loadout preview's unkeyed pixel shader

struct Scene {
    std::unique_ptr<FlatRuntimePrefix> p = std::make_unique<FlatRuntimePrefix>();
    const void *depth = token(0x2000), *dsv = token(0x2001), *gbuf = token(0x1000), *hdr = token(0x4000);
    Scene() { p->frame = kFrame; p->output = token(0x9000); p->width = 1920; p->height = 1200; p->format = 28; }
    // An unkeyed G-buffer writer: a Screen-kind draw no source record tracks.
    FlatRuntimeDraw writer(const void* colour = nullptr, uint64_t vs = 0x114AF608F86D9ED8ull, float nearPlane = .025f, uint32_t format = 23) {
        auto d = draw(colour ? colour : gbuf, format, depth, dsv, vs, kPreviewPs);
        d.key.kind = kFlatContractScreen;
        camera(d, kFrame, p->sequence + 1, nearPlane);
        return d;
    }
    // A supported pool draw: the only thing that makes a source record.
    FlatRuntimeDraw pool(const void* colour, float nearPlane = .025f) {
        auto d = draw(colour, 23, depth, dsv, kPoolVs, kPoolPs);
        d.key.kind = kFlatContractPool; d.supported = true;
        camera(d, kFrame, p->sequence + 1, nearPlane);
        return d;
    }
    // The deferred lighting pass, reading the G-buffer as SRV0.
    FlatRuntimeDraw lighting() const {
        auto d = draw(hdr, 26, depth, dsv, kFlatDeferredHdrVs, kFlatDeferredHdrPs);
        d.key.kind = kFlatContractScreen; d.key.srvResource[0] = gbuf; d.key.srvView[0] = token(0x1002);
        return d;
    }
    void see(const FlatRuntimeDraw& d) { flatRuntimeObserve(*p, d); }
    FlatDeferredDonor donor(const FlatRuntimeDraw& h) const { return flatRuntimeDeferredDonor(*p, h.key, p->sequence + 1); }
    const FlatRuntimeCameraWitness* entry(const void* colour) const {
        for (uint32_t i = 0; i < p->witnessesUsed; ++i)
            if (p->witnesses[i].depth == depth && p->witnesses[i].color == colour) return &p->witnesses[i];
        return nullptr;
    }
};
inline std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
inline size_t countOf(const std::string& text, const std::string& needle) {
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++n;
    return n;
}
inline std::string between(const std::string& text, const std::string& from, const std::string& to) {
    const size_t a = text.find(from);
    if (a == std::string::npos) return {};
    const size_t b = text.find(to, a + from.size());
    return b == std::string::npos ? std::string{} : text.substr(a, b - a);
}

inline unsigned run() {
    unsigned failures = 0, checks = 0;
    const auto expect = [&](bool ok, const char* name) { ++checks; if (!ok) { ++failures; std::printf("FAIL: deferred witness %s\n", name); } };
    using Role = FlatDeferredDonorRole;
    using Why = FlatDeferredDonorWhy;

    // ---- a single camera witness resolves --------------------------------------------------------------------------------------------
    {
        Scene s;
        for (uint64_t vs = 0x100; vs < 0x103; ++vs) s.see(s.writer(nullptr, vs));
        const auto h = s.lighting();
        const auto d = s.donor(h);
        const auto* e = s.entry(s.gbuf);
        expect(d && d.role == Role::Witness && d.why == Why::None, "a single-camera witness resolves with the role witness");
        expect(!d.vetoColor && !d.vetoB1 && !d.vetoHash && d.entryB1 == token(0x3000), "an accepted witness names no veto");
        expect(e && e->writers == 3 && e->first == 1 && e->last == 3 && e->conflict == FlatWitnessConflict::None, "the entry counts its writers and first and last q");
        expect(d.color == s.gbuf && d.b1 == token(0x3000) && d.first == 1 && d.last == 3 && d.writers == 3, "the donor carries the writers' colour, b1 and q range");
        expect(d.camera == e->camera && d.cameraHash == flatCameraHash(d.camera) && d.cameraHash == e->cameraHash, "the donor's camera is the entry's bytes and their hash");
        expect(d.writeEpoch == kFrame && d.writeSeq == 1 && d.lastWriteEpoch == kFrame && d.lastWriteSeq == 3, "the donor carries the camera write provenance of the first and last writer");
        expect(d.poolRecords == 0 && d.witnessEntries == 1 && d.witnessCameras == 1 && d.entry && !d.overflow, "the diagnostics say one entry, one camera, no pool record");
        expect(s.p->sourcesUsed == 0 && s.p->witnessesUsed == 1, "an unkeyed writer makes no source record, only the witness");
        expect(std::string(flatDeferredDonorRoleName(d.role)) == "witness" && std::string(flatDeferredDonorOwnershipName(d.role)) == "witness" &&
               std::string(flatDeferredDonorRoleName(Role::Pool)) == "pool" && std::string(flatDeferredDonorOwnershipName(Role::Pool)) == "qualified-pool" &&
               std::string(flatDeferredDonorRoleName(Role::None)) == "none" && std::string(flatDeferredDonorOwnershipName(Role::None)) == "unavailable",
               "the three roles print as the logs name them");
        // the same camera in several colours on the depth is still one camera
        s.see(s.writer(token(0x1100)));
        const auto two = s.donor(h);
        expect(two.role == Role::Witness && two.witnessEntries == 2 && two.witnessCameras == 1, "the same camera on another colour of the depth is not a second camera");
        // a writer is no witness unless it is a Screen-kind unsupported draw with a depth, a dsv and an RTV
        Scene f;
        auto supported = f.writer(); supported.supported = true; f.see(supported);
        auto none = f.writer(); none.key.kind = kFlatContractNone; f.see(none);
        auto noDepth = f.writer(); noDepth.key.depth = nullptr; noDepth.key.dsv = nullptr; f.see(noDepth);
        auto noDsv = f.writer(); noDsv.key.dsv = nullptr; f.see(noDsv);
        auto noRtv = f.writer(); noRtv.key.rtv = nullptr; f.see(noRtv);
        auto noColour = f.writer(); noColour.key.color = nullptr; f.see(noColour);
        expect(f.p->witnessesUsed == 0 && !f.p->witnessOverflow, "supported, kindless, depthless, dsv-less, rtv-less and colourless draws are never witnesses");
    }
    // ---- two cameras on the depth: none ----------------------------------------------------------------------------------------------
    {
        Scene s;
        s.see(s.writer());
        s.see(s.writer(token(0x1100), 0x200, .0675f));   // a second camera, in another colour of the same depth
        const auto d = s.donor(s.lighting());
        expect(!d && d.role == Role::None && d.why == Why::OtherCamera && d.witnessCameras == 2 && d.witnessEntries == 2, "two cameras on the depth: none");
        expect(d.vetoColor == token(0x1100) && d.vetoB1 == token(0x3000) && d.vetoHash == s.entry(token(0x1100))->cameraHash &&
               d.vetoHash != d.entryCameraHash && d.entryB1 == token(0x3000) && d.entryCameraHash == s.entry(s.gbuf)->cameraHash,
               "the veto names the other camera's colour, b1 and hash beside the entry's own");
        Scene w;   // the weapon preview drawn with its own camera into the HDR target the lighting pass writes
        w.see(w.writer());
        w.see(w.writer(w.hdr, 0x300, .0675f, 26));
        expect(!w.donor(w.lighting()) && w.donor(w.lighting()).why == Why::OtherCamera, "a weapon preview's own camera on the depth: none");
        Scene b1;  // the same bytes behind another constant buffer are another camera too
        b1.see(b1.writer());
        auto other = b1.writer(token(0x1100)); other.key.b1 = token(0x3001); b1.see(other);
        expect(!b1.donor(b1.lighting()) && b1.donor(b1.lighting()).why == Why::OtherCamera, "the same bytes in another constant buffer: none");
        Scene n;   // an entry that carries no camera cannot contradict one
        n.see(n.writer());
        auto bare = n.writer(token(0x1100)); bare.key.camera = nullptr; bare.key.cameraHash = 0; bare.key.b1 = nullptr; n.see(bare);
        expect(n.donor(n.lighting()).role == Role::Witness && n.donor(n.lighting()).witnessCameras == 1, "an entry with no camera is no second camera");
        Scene l;   // a camera on another depth is no business of this one
        l.see(l.writer());
        auto elsewhere = l.writer(token(0x1100), 0x200, .0675f); elsewhere.key.depth = token(0x2100); elsewhere.key.dsv = token(0x2101); l.see(elsewhere);
        expect(l.donor(l.lighting()).role == Role::Witness && l.donor(l.lighting()).witnessEntries == 1, "a second camera on another depth does not veto this one");
    }
    // ---- a different camera on the same key: conflict, none --------------------------------------------------------------------------
    {
        struct Case { const char* name; FlatWitnessConflict reason; void (*change)(FlatRuntimeDraw&); };
        const Case cases[] = {
            {"different camera bytes", FlatWitnessConflict::Camera, [](FlatRuntimeDraw& d) { d.camera[80] ^= 1; d.key.cameraHash = flatCameraHash(d.camera); }},
            {"another constant buffer", FlatWitnessConflict::Camera, [](FlatRuntimeDraw& d) { d.key.b1 = token(0x3001); }},
            {"a stale camera write", FlatWitnessConflict::CameraProvenance, [](FlatRuntimeDraw& d) { d.key.writeEpoch = kFrame - 1; }},
            {"a camera written after the draw", FlatWitnessConflict::CameraProvenance, [](FlatRuntimeDraw& d) { d.key.writeSeq += 50; }},
            {"no camera at all", FlatWitnessConflict::CameraProvenance, [](FlatRuntimeDraw& d) { d.key.camera = nullptr; d.key.cameraHash = 0; }},
            {"a hash that is not the bytes'", FlatWitnessConflict::CameraProvenance, [](FlatRuntimeDraw& d) { d.key.cameraHash ^= 1; }},
            {"a partial viewport", FlatWitnessConflict::Viewport, [](FlatRuntimeDraw& d) { d.key.viewport[2] = 960; }},
            {"a clipped depth range", FlatWitnessConflict::Viewport, [](FlatRuntimeDraw& d) { d.key.viewport[5] = .5f; }},
            {"another dsv", FlatWitnessConflict::Layout, [](FlatRuntimeDraw& d) { d.key.dsv = token(0x2BAD); }},
            {"another extent", FlatWitnessConflict::Layout, [](FlatRuntimeDraw& d) { d.key.width = d.key.depthWidth = 1919; d.key.viewport[2] = 1919; }},
            {"another depth format", FlatWitnessConflict::Layout, [](FlatRuntimeDraw& d) { d.key.depthFormat = 44; }},
            {"another colour format", FlatWitnessConflict::Layout, [](FlatRuntimeDraw& d) { d.key.format = 27; }},
        };
        for (const Case& c : cases) {
            Scene s;
            s.see(s.writer());
            auto bad = s.writer(); c.change(bad); s.see(bad);
            s.see(s.writer());   // an agreeing writer afterwards does not clear it
            const auto* e = s.entry(s.gbuf);
            const auto d = s.donor(s.lighting());
            if (!(e && e->conflict == c.reason && e->writers == 3 && !d && d.why == Why::Conflict && d.conflict == c.reason && d.writers == 3))
                std::printf("deferred witness case `%s`: conflict=%s writers=%u why=%s\n", c.name, e ? flatWitnessConflictName(e->conflict) : "no entry",
                            e ? e->writers : 0, flatDeferredDonorWhyName(d.why));
            expect(e && e->conflict == c.reason && e->writers == 3 && !d && d.why == Why::Conflict && d.conflict == c.reason && d.writers == 3, c.name);
        }
        Scene s;   // the first conflict is the one kept
        s.see(s.writer());
        auto first = s.writer(); first.key.viewport[2] = 960; s.see(first);
        auto second = s.writer(); second.camera[80] ^= 1; second.key.cameraHash = flatCameraHash(second.camera); s.see(second);
        expect(s.entry(s.gbuf)->conflict == FlatWitnessConflict::Viewport, "the first reason for a conflict is kept");
        // a conflict on another colour of the depth that is not about the camera does not veto this one
        Scene t;
        t.see(t.writer());
        t.see(t.writer(token(0x1100)));
        auto layout = t.writer(token(0x1100)); layout.key.dsv = token(0x2BAD); t.see(layout);
        expect(t.entry(token(0x1100))->conflict == FlatWitnessConflict::Layout && t.donor(t.lighting()).role == Role::Witness,
               "another colour's layout conflict is no second camera");
        // a conflict on another colour that IS about the camera, whatever its bytes, does
        Scene u;
        u.see(u.writer());
        u.see(u.writer(token(0x1100)));
        auto stale = u.writer(token(0x1100)); stale.key.writeEpoch = kFrame - 1; u.see(stale);
        expect(!u.donor(u.lighting()) && u.donor(u.lighting()).why == Why::OtherCamera, "another colour's camera conflict vetoes this one");
    }
    // ---- any source record on the depth: the pool rule alone -------------------------------------------------------------------------
    {
        Scene s;   // a valid witness, then a pool record in another colour: the pool rule says no and the witness is ignored
        s.see(s.writer());
        s.see(s.writer());
        s.see(s.pool(token(0x1500)));
        const auto d = s.donor(s.lighting());
        expect(s.p->sourcesUsed == 1 && !d && d.role == Role::None && d.why == Why::PoolRule && d.poolRecords == 1, "a pool record in another colour: the pool rule alone, witness ignored");
        expect(s.entry(s.gbuf)->conflict == FlatWitnessConflict::None && s.entry(s.gbuf)->cameraCurrent, "(the witness itself was valid)");
        const uint32_t writers = s.entry(s.gbuf)->writers;
        s.see(s.writer());
        expect(s.entry(s.gbuf)->writers == writers && s.p->witnessesUsed == 1, "no writer is noted on a depth once a source record is there");
        Scene t;   // a pool record in the colour the pass reads is the pool donor, and it wins over the witness
        t.see(t.writer());
        t.see(t.pool(t.gbuf));
        const auto p = t.donor(t.lighting());
        expect(p && p.role == Role::Pool && p.why == Why::None && p.poolRecords == 1 && p.camera == t.p->sources[0].camera &&
               p.b1 == t.p->sources[0].key.b1 && p.first == t.p->sources[0].first && p.last == t.p->sources[0].last, "a pool record in SRV0 is the pool donor");
        Scene u;   // the existing pool rule on its own
        u.see(u.pool(u.gbuf));
        const auto only = u.donor(u.lighting());
        expect(only && only.role == Role::Pool && only.cameraHash == u.p->sources[0].key.cameraHash && only.camera == u.p->sources[0].camera &&
               only.writeEpoch == kFrame && only.writeSeq == 1 && only.lastWriteSeq == 1, "pool donor: one qualified source in SRV0");
        Scene v;   // two pool records (two cameras) on the depth: still none
        v.see(v.pool(v.gbuf));
        v.see(v.pool(token(0x1100), .0675f));
        expect(!v.donor(v.lighting()) && v.donor(v.lighting()).why == Why::PoolRule && v.donor(v.lighting()).poolRecords == 2, "pool donor: two cameras, none");
        Scene w;   // a source record of any other kind on the depth also holds the witness off
        w.see(w.writer());
        FlatContractRecord other{};
        other.key.depth = w.depth; other.key.kind = kFlatContractScreen; other.key.color = w.gbuf; other.key.rtv = token(0x1001); other.key.dsv = w.dsv;
        w.p->sources[w.p->sourcesUsed++] = other;
        expect(!w.donor(w.lighting()) && w.donor(w.lighting()).why == Why::PoolRule, "a source record of another kind: the pool rule alone, witness ignored");
        Scene x;   // a source record on ANOTHER depth does not
        x.see(x.writer());
        auto elsewhere = x.pool(token(0x1500)); elsewhere.key.depth = token(0x2100); elsewhere.key.dsv = token(0x2101); x.see(elsewhere);
        expect(x.donor(x.lighting()).role == Role::Witness && x.donor(x.lighting()).poolRecords == 0, "a source record on another depth leaves the witness available here");
        // the pool rule's own refusals still refuse, with the witness valid
        Scene y;
        y.see(y.writer());
        auto stale = y.pool(y.gbuf); stale.key.writeEpoch = kFrame - 1; y.see(stale);
        expect(!y.donor(y.lighting()) && y.donor(y.lighting()).why == Why::PoolRule, "a stale pool record refuses and the witness does not rescue it");
    }
    // ---- the witness must be the pass's own SRV0, of another target ------------------------------------------------------------------
    {
        Scene s;   // colour != SRV0
        s.see(s.writer());
        auto h = s.lighting(); h.key.srvResource[0] = token(0x1100);
        expect(!s.donor(h) && s.donor(h).why == Why::NoEntry && s.donor(h).entry == false && s.donor(h).witnessEntries == 1, "witness colour != SRV0: none");
        Scene o;   // the right colour on another depth
        auto other = o.writer(); other.key.depth = token(0x2100); other.key.dsv = token(0x2101); o.see(other);
        expect(!o.donor(o.lighting()) && o.donor(o.lighting()).why == Why::NoEntry, "an entry for SRV0 on another depth: none");
        Scene t;   // colour == the pass's own target
        t.see(t.writer());
        auto self = t.lighting(); self.key.color = t.gbuf; self.key.rtv = token(0x1001);
        expect(!t.donor(self) && t.donor(self).why == Why::SameColor && t.donor(self).entry, "witness colour == the pass's own target: none");
        Scene n;   // nothing at all
        expect(!n.donor(n.lighting()) && n.donor(n.lighting()).why == Why::NoEntry && n.donor(n.lighting()).witnessEntries == 0, "no writers at all: none");
    }
    // ---- layout, viewport and camera of the entry ------------------------------------------------------------------------------------
    {
        Scene s;
        s.see(s.writer());
        s.see(s.writer());
        auto dsv = s.lighting(); dsv.key.dsv = token(0x2BAD);
        auto fmt = s.lighting(); fmt.key.depthFormat = 44;
        auto ext = s.lighting(); ext.key.width = ext.key.depthWidth = 1919; ext.key.viewport[2] = 1919;
        expect(s.donor(dsv).why == Why::Layout && s.donor(fmt).why == Why::Layout && s.donor(ext).why == Why::Layout &&
               !s.donor(dsv) && !s.donor(fmt) && !s.donor(ext), "dsv, depth format or extent differing from the pass's: none");
        auto shape = s.lighting(); shape.key.depthWidth = 1919;
        auto narrow = s.lighting(); narrow.key.viewport[2] = 960;
        expect(s.donor(shape).why == Why::Shape && s.donor(narrow).why == Why::Shape, "a pass whose own depth extent or viewport is not full is not judged");
        Scene v;   // the only writer drew a partial viewport
        auto part = v.writer(); part.key.viewport[3] = 600; v.see(part);
        expect(!v.donor(v.lighting()) && v.donor(v.lighting()).why == Why::Viewport && !v.entry(v.gbuf)->fullViewport, "partial viewport: none");
        Scene c;   // the only writer's camera was not current
        auto old = c.writer(); old.key.writeEpoch = kFrame - 1; c.see(old);
        expect(!c.donor(c.lighting()) && c.donor(c.lighting()).why == Why::Camera && !c.entry(c.gbuf)->cameraCurrent, "camera not current: none");
        Scene m;   // the only writer had no camera
        auto bare = m.writer(); bare.key.camera = nullptr; bare.key.cameraHash = 0; m.see(bare);
        expect(!m.donor(m.lighting()) && m.donor(m.lighting()).why == Why::Camera && !m.entry(m.gbuf)->hasCamera, "no camera: none");
        Scene r;   // last >= q
        r.see(r.writer());
        r.see(r.writer());
        const auto h = r.lighting();
        expect(flatRuntimeDeferredDonor(*r.p, h.key, r.p->sequence).why == Why::Late && !flatRuntimeDeferredDonor(*r.p, h.key, r.p->sequence) &&
               flatRuntimeDeferredDonor(*r.p, h.key, r.p->sequence + 1).role == Role::Witness, "last >= q: none, one later: witness");
        Scene u;   // an uncertain prefix
        u.see(u.writer());
        u.p->uncertain = true;
        expect(!u.donor(u.lighting()) && u.donor(u.lighting()).why == Why::Uncertain, "an uncertain prefix: none");
        Scene w;   // explicit writes
        w.see(w.writer());
        w.see(w.writer(token(0x1100)));
        flatRuntimeWritten(*w.p, token(0x7777));
        expect(w.donor(w.lighting()).role == Role::Witness, "a write to an unrelated resource changes nothing");
        flatRuntimeWritten(*w.p, token(0x1100));
        expect(w.entry(token(0x1100))->writers == 0 && w.entry(token(0x1100))->conflict == FlatWitnessConflict::None &&
               w.donor(w.lighting()).role == Role::Witness, "a write to another colour restarts that entry and is no second camera");
        flatRuntimeWritten(*w.p, w.gbuf);
        expect(!w.donor(w.lighting()) && w.donor(w.lighting()).why == Why::NoEntry && w.entry(w.gbuf)->writers == 0,
               "a write to SRV0 after its writers: none until a writer follows");
        w.see(w.writer());
        expect(w.donor(w.lighting()).role == Role::Witness && w.entry(w.gbuf)->writers == 1, "a writer after the write speaks for SRV0 again");
        Scene lo;   // the loadout order: a prepass binding the G-buffer, a clear of it, then the G-buffer fill
        lo.see(lo.writer(nullptr, 0xF516BF0201303B87ull));
        lo.see(lo.writer(nullptr, 0x0123456789ABCDEFull, .5f));   // a second camera, drawn before the clear
        expect(!lo.donor(lo.lighting()) && lo.entry(lo.gbuf)->conflict == FlatWitnessConflict::Camera, "before the clear, two cameras: none");
        flatRuntimeWritten(*lo.p, lo.gbuf);
        lo.see(lo.writer());
        lo.see(lo.writer(nullptr, 0x8B589D25B2A0ADDCull));
        expect(lo.donor(lo.lighting()).role == Role::Witness && lo.entry(lo.gbuf)->writers == 2,
               "a cleared G-buffer is spoken for by the writers after the clear alone");
        lo.see(lo.writer(nullptr, 0x0123456789ABCDEFull, .5f));
        expect(!lo.donor(lo.lighting()) && lo.entry(lo.gbuf)->conflict == FlatWitnessConflict::Camera, "a second camera after the clear: none");
        Scene z;
        z.see(z.writer());
        flatRuntimeWritten(*z.p, z.depth);
        expect(!z.donor(z.lighting()) && z.donor(z.lighting()).conflict == FlatWitnessConflict::Written, "a write to the depth: none");
        flatRuntimeWritten(*z.p, z.gbuf);
        z.see(z.writer());
        expect(!z.donor(z.lighting()) && z.entry(z.gbuf)->conflict == FlatWitnessConflict::Written,
               "a colour write after a depth write does not undo it");
        // pairs that never read VS b1 (flatProjectionB1Unread) neither open nor conflict an entry, whatever b1 they inherit
        const auto filter = [](Scene& sc, const void* colour, uint64_t vs, uint64_t ps, float nearPlane) {
            auto d = draw(colour, 60, sc.depth, sc.dsv, vs, ps);
            d.key.kind = kFlatContractScreen;
            camera(d, kFrame, sc.p->sequence + 1, nearPlane);
            return d;
        };
        Scene sr;  // the loadout's sun-shadow resolve after an auxiliary scene left its camera in b1 (flat_trace_20491)
        sr.see(sr.writer());
        sr.see(filter(sr, token(0x1300), 0x53211E8C072CD02Eull, 0x7EAC71963E66C5FEull, .5f));
        sr.see(filter(sr, token(0x1300), 0x53211E8C072CD02Eull, 0xB403F48CB35D9739ull, .5f));
        expect(sr.donor(sr.lighting()).role == Role::Witness && !sr.entry(token(0x1300)) && sr.donor(sr.lighting()).witnessCameras == 1,
               "the b1-free sun-shadow pairs with a stale camera: no entry, no veto");
        sr.see(filter(sr, token(0x1300), 0x53211E8C072CD02Eull, 0x7EAC71963E66C5FFull, .5f));
        expect(!sr.donor(sr.lighting()) && sr.donor(sr.lighting()).why == Why::OtherCamera, "one PS bit away it is a second camera: none");
        Scene ia;  // an inert pair writing SRV0 first opens no entry
        ia.see(filter(ia, ia.gbuf, 0xE8FDC0D92EEBA6D7ull, 0x258B95AC99520C1Full, .5f));
        expect(!ia.entry(ia.gbuf) && !ia.donor(ia.lighting()) && ia.donor(ia.lighting()).why == Why::NoEntry, "an inert pair alone on SRV0: none");
        ia.see(ia.writer());
        expect(ia.donor(ia.lighting()).role == Role::Witness && ia.entry(ia.gbuf)->writers == 1, "the first camera writer after it opens the entry");
        expect(flatProjectionB1Unread(0x53211E8C072CD02Eull, 0x7EAC71963E66C5FEull) && flatProjectionB1Unread(0xFC1193AFFC596F74ull, 0x258B95AC99520C1Full) &&
               !flatProjectionB1Unread(0x53211E8C072CD02Eull, 0x7EAC71963E66C5FFull) && !flatProjectionB1Unread(0x114AF608F86D9ED8ull, kPreviewPs),
               "b1-free pairs are exact");
        Scene y;   // a compute pass writing the G-buffer through a UAV
        y.see(y.writer());
        flatRuntimeDispatchObserveWritten(*y.p, token(0x7777));
        expect(y.donor(y.lighting()).role == Role::Witness, "a UAV write to an unrelated resource changes nothing");
        flatRuntimeDispatchObserveWritten(*y.p, y.gbuf);
        expect(!y.donor(y.lighting()) && y.donor(y.lighting()).why == Why::NoEntry, "a UAV write to SRV0 after its writers: none");
    }
    // ---- table overflow fails closed -------------------------------------------------------------------------------------------------
    {
        Scene s;
        s.see(s.writer());
        for (uintptr_t i = 1; i < kFlatRuntimeWitnesses; ++i) s.see(s.writer(token(0x1100 + i * 0x10)));
        expect(s.p->witnessesUsed == kFlatRuntimeWitnesses && !s.p->witnessOverflow && s.donor(s.lighting()).role == Role::Witness &&
               s.donor(s.lighting()).witnessEntries == kFlatRuntimeWitnesses, "a full table is not an overflow");
        s.see(s.writer(token(0x9100)));
        const auto d = s.donor(s.lighting());
        expect(s.p->witnessOverflow && s.p->witnessesUsed == kFlatRuntimeWitnesses && !d && d.why == Why::Overflow && d.overflow, "table overflow: none, though SRV0's entry is valid");
        Scene t;   // writers into entries that already exist never overflow
        t.see(t.writer());
        for (int i = 0; i < 100; ++i) t.see(t.writer());
        expect(!t.p->witnessOverflow && t.entry(t.gbuf)->writers == 101, "a hundred writers of one key are one entry");
    }
    // ---- end to end through the reducer and the trace --------------------------------------------------------------------------------
    {
        // negative: 1 a second camera on the depth, 2 the pass's own camera is not the writers', 3 an unverified pass, 4 a pool source competes
        const auto flow = [&](unsigned negative, bool viaTrace) {
            Scene s;
            const auto feed = [&](const FlatRuntimeDraw& d) {
                if (viaTrace) { auto event = flatTraceEventFromDraw(d, false); s.see(flatTraceEventToDraw(event)); }
                else s.see(d);
            };
            for (uint64_t vs = 0x100; vs < 0x104; ++vs) feed(s.writer(nullptr, vs));
            if (negative == 1) feed(s.writer(token(0x1100), 0x200, .0675f));
            if (negative == 4) feed(s.pool(token(0x1500)));
            auto h = s.lighting();
            h.deferredHdrVerified = negative != 3;
            std::memcpy(h.camera, s.p->witnesses[0].camera, sizeof(h.camera));
            if (negative == 2) h.camera[80] ^= 1;
            h.key.cameraHash = flatCameraHash(h.camera);
            auto& projection = h.key.projection[1];
            projection.resource = token(0x6000); projection.width = 784; projection.copied = 112; projection.hash = 123;
            projection.status = FlatProjectionStatus::Available; projection.writeEpoch = kFrame; projection.writeSeq = 2;
            feed(h);
            const auto* t = flatRuntimeTarget(*s.p, s.hdr);
            const bool qualified = t && t->hdrCamera && t->tone.deferredCamera && !t->hdrBad;
            if (!negative) {
                expect(qualified && t->tone.sceneCameraB1 == token(0x3000) && t->tone.sceneCameraWriteEpoch == kFrame && t->tone.sceneCameraWriteSeq == 1 &&
                       flat_mono_detail::cameraCurrent(t->tone, kFrame) && t->tone.first == 5,
                       viaTrace ? "the reducer qualifies the lighting pass on the witness, through the trace" : "the reducer qualifies the lighting pass on the witness");
            } else {
                if (qualified) std::printf("deferred witness negative case %u qualified\n", negative);
                expect(!qualified, "the reducer refuses a competing camera, a different pass camera, an unverified pass and a pool source");
            }
        };
        for (const bool viaTrace : {false, true}) for (unsigned negative = 0; negative <= 4; ++negative) flow(negative, viaTrace);
    }
    // ---- the three callers share the one resolver, and the logs name it --------------------------------------------------------------
    {
        const std::string runtime = slurp("src/d3d11/flat_runtime.cpp"), model = slurp("src/d3d11/flat_runtime_model.h");
        expect(!runtime.empty() && !model.empty(), "the sources are readable from the repo root");
        size_t other = 0;
        for (std::filesystem::recursive_directory_iterator it("src"), end; it != end; ++it) {
            const std::string name = it->path().filename().string();
            const std::string ext = it->path().extension().string();
            if (!it->is_regular_file() || name == "flat_runtime.cpp" || name == "flat_runtime_model.h") continue;
            if (ext == ".h" || ext == ".cpp" || ext == ".inl") other += countOf(slurp(it->path().string()), "flatRuntimeDeferredDonor(");
        }
        expect(other == 0, "no other file asks for a deferred donor");
        const std::string verifier = between(runtime, "const FlatProjectionBindingPlan* verifyDeferredHdrCamera(", "void reportHdrProjectionCapture(");
        const std::string binding = between(runtime, "        if(!deferredProjection->active()) {", "    // Typed light authority");
        const std::string qualified = between(model, "inline bool flatRuntimeQualifiedDeferredCamera(", "// Runtime's exact-resource/used-transform attestation");
        const std::string observe = between(model, "if(flatRuntimeQualifiedDeferredCamera(p,*t,d,q)) {", "if (k.camera) {");
        expect(countOf(verifier, "flatRuntimeDeferredDonor(") == 1 && countOf(binding, "flatRuntimeDeferredDonor(") == 1 && countOf(runtime, "flatRuntimeDeferredDonor(") == 2,
               "the verifier and its binding-failure path ask the shared resolver, once each, and nothing else in the runtime does");
        expect(countOf(qualified, "flatRuntimeDeferredDonor(") == 1 && countOf(observe, "flatRuntimeDeferredDonor(") == 1 && countOf(model, "flatRuntimeDeferredDonor(") == 3,
               "the model's qualification and the donor it records ask the same resolver (a definition and two uses)");
        expect(model.find("inline FlatDeferredDonor flatRuntimeDeferredDonor(") != std::string::npos &&
               countOf(model, "r.key.color!=k.srvResource[0] || r.key.color==k.color") == 1, "the pool rule for the deferred pass is written once, in the resolver");
        expect(countOf(runtime, "flat deferred witness:") == 1 && countOf(runtime, "donor-role=%s") == 3 &&
               verifier.find("donor-role=%s source=%p") != std::string::npos && verifier.find("flatDeferredDonorRoleName(donor.role)") != std::string::npos,
               "the HDR camera line, the ownership line and the witness line each name the donor role once");
        expect(runtime.find("flatDeferredDonorOwnershipName(donor.role)") != std::string::npos &&
               verifier.find("uint64_t& witnessLastLog=donor?s.deferredWitnessLastLog:s.deferredWitnessRefusedLastLog;") != std::string::npos &&
               verifier.find("if(!donor.poolRecords && flatDeferredFailureWitnessDue(++s.deferredWitnessCalls,now,witnessLastLog)) {") != std::string::npos &&
               verifier.find("witnessLastLog=now;") != std::string::npos,
               "the witness line is printed only where the pool rule had no record, first occurrence and then every 5 s, refusals on their own budget");
        // the witness line is written after the relation is known and before the HDR camera line, inside the verifier's one exit
        const auto relation = verifier.find("const auto match=donor && copied?");
        const auto line = verifier.find("flat deferred witness:");
        const auto camera = verifier.find("flat deferred HDR camera:");
        expect(relation != std::string::npos && line != std::string::npos && camera != std::string::npos && relation < line && line < camera,
               "the witness line follows the relation it reports and precedes the HDR camera line");
        // the hook that fills the table is inside the reducer, after the source records, for the draws the table names
        const std::string hook = between(model, "} else if (!d.supported && k.kind == kFlatContractScreen", "return out;");
        expect(!hook.empty() && hook.find("k.depth && k.dsv && k.rtv") != std::string::npos && hook.find("flatRuntimeNoteWitness(p, d, q)") != std::string::npos,
               "the reducer notes Screen-kind unsupported writers with a depth, a dsv and an RTV");
        expect(model.find("if (e && e->conflict != FlatWitnessConflict::None) { ++e->writers; e->last = q; return; }") != std::string::npos &&
               model.find("for (uint32_t i = 0; i < p.sourcesUsed; ++i) if (p.sources[i].key.depth == k.depth) return;") != std::string::npos,
               "a source record on the depth ends the noting, and a conflicted entry costs two stores");
    }
    std::printf("flat deferred witness resolver: %u checks, %u failure(s)\n", checks, failures);
    return failures;
}
} // namespace deferred_witness_test
