// The optimized station's sun-skip probe (built 2026-10-21; TEMPORARY, goes with diag.station_sun_skip when the Orbis cockpit arc closes;
// docs\optimized-station-2026-10-12.md, Status). Log-only: it records what the skipped sun draws were bound to and says it in one line every
// 5 s, and it never changes a draw.
//
// Pure and free of any Windows API, so tools\optimized_station_test checks the recording, the comparison and the line's shape.
//
// What the line answers. The Orbis flight had the sun skip running with no seal and the cockpit's flat AA refusing every frame
// (conflicting-hdr-target-or-camera, cause image-copy-source). Hypothesis S1: a skipped sun draw writes into the HDR target the flat runtime
// names (`hdr=`), and its absence is what the conflict sees. The line carries, for each skipped draw of one sampled frame, the colour target
// it was bound to (view, texture, format, size), the depth view, the viewport and where it fell in the flat runtime's draw numbering, and then
// says whether that texture is the conflict's `hdr=` and whether the draw fell between the conflict's ref-q and cur-q.
//
// q. The flat runtime numbers a draw (`q`) when its draw scope observes it. Since the sun skip no longer returns before the scope (2026-10-21), the
// skipped draw is observed like any draw and carries its own q: the line prints `q=N` and "between ref-q and cur-q" means ref-q <= q < cur-q (the
// draw is the reference draw itself or falls between). When the scope did not number the draw (a paused or inactive frame, a draw the scope serves
// without observing) the probe records `after-q`, the highest q numbered when the draw was met: the draw then sits after the draw q=after-q and before
// q=after-q+1, and "between" means ref-q <= after-q < cur-q.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace edvr::sunprobe {

constexpr uint32_t kMaxDraws = 6;          // draws carried in a line
constexpr uint64_t kSampleEveryMs = 5000;  // one sampled frame this often
constexpr uint64_t kStaleAfterMs = 10000;  // armed this long with no skipped draw recorded: say so (the probe may not be reached)
constexpr uint32_t kMaxStaleLines = 6;     // and at most this many times in a session

// One skipped draw's bindings. Identities are compared, never dereferenced.
struct Draw {
    bool qKnown = false;       // the flat runtime's sequence was readable on this thread (else q is not known)
    bool ownQ = false;         // the scope numbered this draw: afterQ holds its own q (the line says `q=`); else afterQ is the last q numbered (`after-q=`)
    uint32_t afterQ = 0;
    uint64_t rtv = 0;          // render target view in slot 0
    uint64_t tex = 0;          // its texture (what the flat runtime calls the HDR target is a texture)
    uint64_t dsv = 0;
    bool sized = false;        // the view resolved to a 2D texture: fmt, width, height are real
    uint32_t fmt = 0, width = 0, height = 0;
    uint32_t vpCount = 0;      // viewports bound; vp is the first one
    float vp[4] = {};          // x, y, width, height
};

// The draws skipped in one frame. `tick` is the station tick count the draws were issued under (the frame is over once the tick has moved on),
// `frame` the game frame the flat runtime numbers it by (0 when the runtime could not say). `total` counts all of them, `n` is how many are
// carried (kMaxDraws at most).
struct Frame {
    uint32_t tick = 0;
    uint64_t frame = 0;
    uint32_t total = 0;
    uint32_t n = 0;
    Draw d[kMaxDraws];
};

// The flat runtime's selected conflict for a frame, as the frame's `flat runtime conflict` line carries it.
struct Conflict {
    bool have = false;
    uint64_t frame = 0;
    const char* cause = "";
    uint64_t hdr = 0;          // the conflict's `hdr=`
    uint32_t refQ = 0, curQ = 0;
};

// Adds a draw to the frame being recorded. The first draw fixes the frame; a draw of another tick is refused while this one has not been
// logged (one sampled frame, never a mix). True when the draw was counted.
inline bool record(Frame* f, uint32_t tick, uint64_t frame, const Draw& d) noexcept {
    if (f->total == 0) {
        f->tick = tick;
        f->frame = frame;
        f->n = 0;
    } else if (f->tick != tick) {
        return false;
    }
    ++f->total;
    if (f->n < kMaxDraws) f->d[f->n++] = d;
    return true;
}

// This draw's colour texture is the conflict's HDR target.
inline bool onHdr(const Draw& d, const Conflict& c) noexcept { return c.have && c.hdr != 0 && d.tex == c.hdr; }

// This draw fell after the conflict's reference draw and before its current one (ref-q <= after-q < cur-q); false when q is not known.
inline bool betweenQ(const Draw& d, const Conflict& c) noexcept { return c.have && d.qKnown && c.refQ <= d.afterQ && d.afterQ < c.curQ; }

// The probe line. Returns the length written (always NUL-terminated within `cap`). A frame carries at most kMaxDraws draws; when the line
// would not fit the buffer the draws that fit are written and "..." says so.
inline size_t formatLine(char* out, size_t cap, const Frame& f, const Conflict& c) noexcept {
    if (cap == 0) return 0;
    size_t used = 0;
    const auto put = [&](const char* fmt, auto... args) -> bool {
        if (used >= cap) return false;
        const int n = std::snprintf(out + used, cap - used, fmt, args...);
        if (n < 0 || static_cast<size_t>(n) >= cap - used) {
            out[used] = '\0';
            return false;
        }
        used += static_cast<size_t>(n);
        return true;
    };
    out[0] = '\0';
    if (f.frame) put("optimized station: sun skip probe (frame %llu): %u draws skipped this frame:", static_cast<unsigned long long>(f.frame), f.total);
    else put("optimized station: sun skip probe (frame ?): %u draws skipped this frame:", f.total);
    // Reserve room for the closing clause so a long draw list never cuts it off.
    const size_t reserve = 200;
    for (uint32_t i = 0; i < f.n; ++i) {
        const Draw& d = f.d[i];
        char one[256];
        int n = 0;
        if (d.qKnown) n = std::snprintf(one, sizeof(one), d.ownQ ? "%s q=%u" : "%s after-q=%u", i ? ";" : "", d.afterQ);
        else n = std::snprintf(one, sizeof(one), "%s after-q=?", i ? ";" : "");
        n += std::snprintf(one + n, sizeof(one) - static_cast<size_t>(n), " rtv=%016llX tex=%016llX", static_cast<unsigned long long>(d.rtv),
                           static_cast<unsigned long long>(d.tex));
        if (d.sized) n += std::snprintf(one + n, sizeof(one) - static_cast<size_t>(n), " fmt=%u %ux%u", d.fmt, d.width, d.height);
        else n += std::snprintf(one + n, sizeof(one) - static_cast<size_t>(n), " fmt=? ?x?");
        n += std::snprintf(one + n, sizeof(one) - static_cast<size_t>(n), " dsv=%016llX vp=%u(%.5g,%.5g,%.5g,%.5g)", static_cast<unsigned long long>(d.dsv),
                           d.vpCount, d.vp[0], d.vp[1], d.vp[2], d.vp[3]);
        if (c.have) {
            n += std::snprintf(one + n, sizeof(one) - static_cast<size_t>(n), " [hdr=%s between-q=%s]", onHdr(d, c) ? "yes" : "no",
                               d.qKnown ? (betweenQ(d, c) ? "yes" : "no") : "?");
        }
        if (used + static_cast<size_t>(n) + reserve >= cap) {
            put(" ...");
            break;
        }
        put("%s", one);
    }
    if (f.total > f.n) put(" (+%u more not listed)", f.total - f.n);
    if (c.have) {
        put("; flat conflict (frame %llu): cause=%s hdr=%016llX ref-q=%u cur-q=%u", static_cast<unsigned long long>(c.frame), c.cause ? c.cause : "?",
            static_cast<unsigned long long>(c.hdr), c.refQ, c.curQ);
    } else {
        put("; flat conflict: none recorded for this frame");
    }
    return used;
}

// When to arm a sampled frame and when to log it. The draw path records only while armed; the tick (once a frame) arms and logs. An arm that no draw
// answers for kStaleAfterMs is said (kStale): without it a probe the draw path never reaches would look the same in the log as a skip that never ran.
struct Sampler {
    uint64_t nextMs = 0;       // the earliest time the next sample may be armed
    uint64_t armedMs = 0;      // when it was armed (or last said stale)
    uint32_t staleLines = 0;
    bool armed = false;
    enum class Act { kNone, kArm, kLog, kStale };
    // `rec` is the frame recorded so far (total 0 when nothing yet), `tick` the station's tick count, already advanced for this tick (so a record
    // stamped with an earlier count is a finished frame), `skipLive` whether the sun skip is skipping now.
    Act step(const Frame& rec, uint32_t tick, uint64_t nowMs, bool skipLive) noexcept {
        if (rec.total > 0 && rec.tick != tick) {
            armed = false;
            nextMs = nowMs + kSampleEveryMs;
            return Act::kLog;
        }
        if (!armed && rec.total == 0 && skipLive && nowMs >= nextMs) {
            armed = true;
            armedMs = nowMs;
            return Act::kArm;
        }
        if (armed && rec.total == 0 && nowMs - armedMs >= kStaleAfterMs && staleLines < kMaxStaleLines) {
            ++staleLines;
            armedMs = nowMs;
            return Act::kStale;
        }
        return Act::kNone;
    }
};

}  // namespace edvr::sunprobe
