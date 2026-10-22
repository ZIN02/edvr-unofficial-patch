#pragma once

// The star dump: which of a star's draw constants carry its temperature or colour, radius and intensity.
//
// OBSERVE ONLY. A planned star glow would strengthen the bloom around a star by its class; the class has to be read from the engine's own
// draws, and which constants hold it is not known. This logs them, and changes no pixel and no behaviour: it reads the context's bindings
// and the flat projection runtime's CPU shadow of the constant buffers (flatRuntimeObserveConstants), and writes log lines.
//
// The trigger is the draw census (dump_draws, Home in the flat profile): every draw the census records whose vertex shader is in the solar list
// or whose pixel shader is in the solar-pixel list (EyeDrawSnapshot::solarDraw / solarPixel, measured in VR) is dumped, up to kMaxDraws per
// census, with its state and the contents of the vertex and pixel constant buffers b0..b7, up to kBufferCap bytes each. The census header
// carries the journal's account of the star (journal_star.h), so each dump brings its own ground truth.
//
// What the log shows, so a missing line is read for what it is (the lines all begin "flat star"):
//   no "flat star dump: begin" in a census   the dump never ran (a build without it, the VR profile, or a census that began before the flat
//                                            profile): not "no star on screen"
//   begin, then end with star-draws=0        the dump ran and the census recorded no draw in the solar lists: no star on screen, or the flat
//                                            star draws are not in the lists (compare the census's own DCO lines, and the one-time
//                                            "flat star first sight" line, which is written the first time any such draw is seen at all)
//   end with star-draws=N dumped=0           the draws were seen and the dump failed (faults=)
//   cb lines "unshadowed"                    the projection runtime held no CPU copy of that buffer; the reason is named
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "eye_draw_snapshot.h"

struct ID3D11DeviceContext;

namespace edvr {
namespace stardump {

constexpr uint32_t kMaxDraws = 32;      // star draws dumped per census (one Home press); the rest are counted as skipped
constexpr uint32_t kSlots = 8;          // constant buffer slots 0..7, both stages
constexpr uint32_t kBufferCap = 1024;   // bytes dumped per buffer, from the start of the bound window
constexpr uint32_t kLineBytes = 128;    // bytes per data line: 32 words, so a line stays under the log-line gate
constexpr uint32_t kWordsPerLine = kLineBytes / 4;
// The text of one data line's words: 32 x 8 hex digits and 31 spaces, and the same words as floats (%.6g: at most 12 characters, nan(ind)
// 9) with a space between. The runtime declares its buffers with these sizes as literals so the log-line gate can read them.
constexpr uint32_t kHexChars = 288;
constexpr uint32_t kFloatChars = 32 * 13;

inline bool solarVertex(uint64_t vs) { return vs && EyeDrawSnapshot::solarDraw(vs); }
inline bool solarPixel(uint64_t ps) { return ps && EyeDrawSnapshot::solarPixel(ps); }
// The selection: a draw is a star draw when its vertex shader is in the solar list or its pixel shader is in the solar-pixel list.
inline bool solarPair(uint64_t vs, uint64_t ps) { return solarVertex(vs) || solarPixel(ps); }

// How many data lines a buffer of `bytes` bytes takes, capped at kBufferCap.
inline uint32_t dumpBytes(uint32_t windowBytes) { return windowBytes < kBufferCap ? windowBytes : kBufferCap; }
inline uint32_t dataLines(uint32_t bytes) { return (dumpBytes(bytes) + kLineBytes - 1) / kLineBytes; }

// One data line: `n` bytes (at most kLineBytes) as little-endian words in hex, and as floats. A ragged tail is zero-padded to a whole word.
// Returns the words written, 0 when a buffer is too small.
inline uint32_t formatWords(char* hex, size_t hexCap, char* fl, size_t flCap, const unsigned char* bytes, uint32_t n) {
    if (!hex || !fl || !bytes || n == 0 || n > kLineBytes || hexCap < kHexChars + 1 || flCap < kFloatChars + 1) return 0;
    const uint32_t words = (n + 3) / 4;
    size_t h = 0, f = 0;
    for (uint32_t i = 0; i < words; ++i) {
        unsigned char w[4] = {0, 0, 0, 0};
        const uint32_t take = n - i * 4 < 4 ? n - i * 4 : 4;
        std::memcpy(w, bytes + i * 4, take);
        uint32_t u;
        std::memcpy(&u, w, 4);
        float v;
        std::memcpy(&v, w, 4);
        const int a = std::snprintf(hex + h, hexCap - h, i ? " %08X" : "%08X", u);
        const int b = std::snprintf(fl + f, flCap - f, i ? " %.6g" : "%.6g", static_cast<double>(v));
        if (a < 0 || b < 0) return 0;
        h += static_cast<size_t>(a);
        f += static_cast<size_t>(b);
    }
    return words;
}

// The slots of a bound-buffer array as "0,1,3", or "-" for none.
inline void slotList(const void* const* bound, uint32_t slots, char* out, size_t cap) {
    size_t at = 0;
    if (!out || cap < 2) return;
    out[0] = '\0';
    for (uint32_t i = 0; i < slots; ++i) {
        if (!bound[i]) continue;
        const int n = std::snprintf(out + at, cap - at, at ? ",%u" : "%u", i);
        if (n < 0 || static_cast<size_t>(n) >= cap - at) break;
        at += static_cast<size_t>(n);
    }
    if (!at) std::snprintf(out, cap, "-");
}

// One census's budget and tallies. take() is asked once for each star draw the census records.
struct Census {
    uint32_t number = 0;      // the census this is (draw_census.cpp's numbering)
    uint32_t seen = 0;        // star draws the census recorded
    uint32_t dumped = 0;
    uint32_t skipped = 0;     // seen past kMaxDraws
    uint32_t capMissed = 0;   // star draws the census's own line cap kept from being recorded
    uint32_t faults = 0;      // dumps the fault guard stopped
    uint32_t buffers = 0, shadowed = 0, unshadowed = 0, duplicate = 0;
    bool take() {
        ++seen;
        if (dumped >= kMaxDraws) { ++skipped; return false; }
        ++dumped;
        return true;
    }
};

// Buffers already dumped in full in this census, by the hash of their dumped bytes: a scene block bound to every star draw is written once
// and named by the later ones.
struct Seen {
    uint64_t hash = 0;
    uint32_t bytes = 0, q = 0, slot = 0;
    bool pixel = false;
};
struct SeenSet {
    static constexpr uint32_t kMax = 128;
    Seen e[kMax];
    uint32_t n = 0;
    const Seen* find(uint64_t hash, uint32_t bytes) const {
        for (uint32_t i = 0; i < n; ++i)
            if (e[i].hash == hash && e[i].bytes == bytes) return &e[i];
        return nullptr;
    }
    void add(uint64_t hash, uint32_t bytes, uint32_t q, uint32_t slot, bool pixel) {
        if (n < kMax) e[n++] = Seen{hash, bytes, q, slot, pixel};
    }
    void clear() { n = 0; }
};

}  // namespace stardump

// What the census knows of one recorded draw (draw_census.cpp's recordDraw): its place in the census and its arguments.
struct StarDrawFacts {
    const char* tag = "";      // DC or DCO, the census line's own tag
    uint32_t frame = 0;        // the census's frame ordinal
    uint32_t index = 0;        // the line's #index
    uint32_t q = 0;            // the census's shared ordinal, the q= of the line
    char kind = '?';           // D, I, N, X
    uint32_t count = 0, instances = 0, start = 0;
    int32_t base = 0;
    uint64_t vs = 0, ps = 0;   // the content hashes the binding shadow holds for the bound pair
    const char* rtv = "-";     // the census's @N tokens for the render target and depth views
    const char* dsv = "-";
};

namespace stardetail {
inline std::atomic<bool> g_firstSightDone{false};
}

// The first draw this session in a solar list, flat profile, logged once. The test is two shadow hashes and a compare until it fires.
void flatStarDumpFirstSight(uint64_t vs, uint64_t ps, char kind, uint32_t count, uint32_t instances);
inline bool flatStarFirstSightDone() { return stardetail::g_firstSightDone.load(std::memory_order_relaxed); }
inline void flatStarDumpNoteDraw(uint64_t vs, uint64_t ps, char kind, uint32_t count, uint32_t instances) {
    if (flatStarFirstSightDone() || !stardump::solarPair(vs, ps)) return;
    flatStarDumpFirstSight(vs, ps, kind, count, instances);
}

// The census hooks (draw_census.cpp). Begin and End write their lines in every flat census, with or without a star; Draw is called for each
// recorded draw the pair test selects; CapMissed for each selected draw the census's line cap kept from being recorded. All of them do
// nothing outside the flat profile.
void flatStarDumpBegin(uint32_t census, uint32_t frames, uint32_t frameNo);
void flatStarDumpDraw(ID3D11DeviceContext* ctx, const StarDrawFacts& facts);
void flatStarDumpCapMissed();
void flatStarDumpEnd(uint32_t census);

}  // namespace edvr
