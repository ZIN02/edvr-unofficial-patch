#pragma once

// The star dump (src\d3d11\flat_star_dump.h, .cpp): which draws it selects, how a buffer becomes log lines, how much it may write, and the
// wiring that makes it observe-only and makes a census that never ran it different from a census with no star on screen.
//
//   1. the selection: every hash in EyeDrawSnapshot::solarDraw / solarPixel (read from the source, so a list that grows is held to the
//      same test) selects its draw, alone and with an unlisted partner; an unlisted pair and a zero pair do not;
//   2. the lines: a 128-byte block is 32 words in hex and as floats, a ragged tail is padded, the worst block stays under the line budget,
//      a buffer too small returns nothing rather than overrunning, and a buffer is dumped to at most kBufferCap bytes in whole lines;
//   3. the budget: 32 star draws are dumped in a census, the rest are counted as skipped; a buffer already dumped is named, not repeated;
//   4. the log-line gate reaches the dump's notes (they begin "flat star"), and the data line is held to the budget;
//   5. the wiring, in the source: begin and end are written by every flat census (the never-ran signature), the draw hook follows the census
//      line and a capped census counts the star draws it kept out, the first sight is one flag load once fired, and nothing in the dump or
//      the shadow accessor writes, binds, copies or reads back on the GPU; the journal's line parser touches none of the published answers.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "../../src/d3d11/flat_star_dump.h"
#include "flat_log_line_tests.h"

namespace flat_star_dump_rig {

inline std::string slurp(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
inline size_t count(const std::string& text, const std::string& needle) {
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++n;
    return n;
}
// The text between two markers (the first `from`, then the first `to` after it), or empty.
inline std::string between(const std::string& text, const std::string& from, const std::string& to) {
    const size_t a = text.find(from);
    if (a == std::string::npos) return std::string();
    const size_t b = text.find(to, a + from.size());
    return b == std::string::npos ? std::string() : text.substr(a, b - a);
}
// The 64-bit hex literals inside the body that starts at `from` (up to its closing brace at column 4).
inline std::vector<uint64_t> literalsIn(const std::string& body) {
    std::vector<uint64_t> out;
    for (size_t at = body.find("0x"); at != std::string::npos; at = body.find("0x", at + 2)) {
        size_t j = at + 2;
        while (j < body.size() && std::isxdigit(static_cast<unsigned char>(body[j]))) ++j;
        if (j - at - 2 == 16) out.push_back(std::strtoull(body.substr(at + 2, 16).c_str(), nullptr, 16));
    }
    return out;
}

inline int run() {
    using namespace edvr;
    int failures = 0;
    const auto expect = [&](bool ok, const std::string& what) {
        if (!ok) { std::printf("FAIL: star dump %s\n", what.c_str()); ++failures; }
    };

    // ===== 1. the selection ==================================================================================================================
    const std::string snapshot = slurp("src/d3d11/eye_draw_snapshot.h");
    expect(snapshot.size() > 50000, "eye_draw_snapshot.h is read from the repo root");
    const std::vector<uint64_t> vsList = literalsIn(between(snapshot, "static bool solarDraw(uint64_t vs)", "static bool solarPixel("));
    const std::vector<uint64_t> psList = literalsIn(between(snapshot, "static bool solarPixel(uint64_t ps)", "// 20:02:56 crouching rifle"));
    expect(!vsList.empty() && !psList.empty(), "both solar lists are found in the source (" + std::to_string(vsList.size()) + " VS, " +
                                                    std::to_string(psList.size()) + " PS)");
    const std::set<uint64_t> vsSet(vsList.begin(), vsList.end()), psSet(psList.begin(), psList.end());
    const uint64_t unlistedA = 0x1234567890ABCDEFull, unlistedB = 0x0FEDCBA987654321ull;
    expect(vsSet.count(unlistedA) == 0 && psSet.count(unlistedA) == 0 && vsSet.count(unlistedB) == 0 && psSet.count(unlistedB) == 0,
           "the two hashes used as 'unlisted' are in neither list");
    for (uint64_t vs : vsList) {
        expect(stardump::solarVertex(vs) && stardump::solarPair(vs, unlistedA) && stardump::solarPair(vs, 0),
               "a listed VS selects its draw whatever the PS is");
        expect(!stardump::solarVertex(vs ^ 1ull) || vsSet.count(vs ^ 1ull), "a hash one bit away from a listed VS is not listed");
    }
    for (uint64_t ps : psList) {
        expect(stardump::solarPixel(ps) && stardump::solarPair(unlistedA, ps) && stardump::solarPair(0, ps),
               "a listed PS selects its draw whatever the VS is");
        expect(!stardump::solarPixel(ps ^ 1ull) || psSet.count(ps ^ 1ull), "a hash one bit away from a listed PS is not listed");
    }
    expect(!stardump::solarPair(unlistedA, unlistedB) && !stardump::solarPair(0, 0), "an unlisted pair and an unknown (zero) pair do not select");
    expect(!stardump::solarPixel(*vsList.begin()) || psSet.count(*vsList.begin()), "a VS hash is not read as a PS hash");

    // ===== 2. the lines ======================================================================================================================
    {
        unsigned char block[stardump::kLineBytes];
        for (uint32_t i = 0; i < sizeof(block); ++i) block[i] = static_cast<unsigned char>(i);
        char hex[300], fl[440];
        const uint32_t words = stardump::formatWords(hex, sizeof(hex), fl, sizeof(fl), block, stardump::kLineBytes);
        expect(words == 32, "a 128-byte block is 32 words");
        expect(std::strncmp(hex, "03020100 07060504", 17) == 0 && std::strlen(hex) == 287, "the words are little-endian, eight hex digits, one space between");
        float one = 1.0f, minusTwo = -2.5f;
        unsigned char two[8];
        std::memcpy(two, &one, 4);
        std::memcpy(two + 4, &minusTwo, 4);
        stardump::formatWords(hex, sizeof(hex), fl, sizeof(fl), two, 8);
        expect(std::strcmp(hex, "3F800000 C0200000") == 0 && std::strcmp(fl, "1 -2.5") == 0, "a word is also printed as the float it holds");
        const uint32_t ragged = stardump::formatWords(hex, sizeof(hex), fl, sizeof(fl), block, 6);
        expect(ragged == 2 && std::strcmp(hex, "03020100 00000504") == 0, "a ragged tail is padded to a whole word with zero");
        expect(stardump::formatWords(hex, 100, fl, sizeof(fl), block, stardump::kLineBytes) == 0 &&
                   stardump::formatWords(hex, sizeof(hex), fl, 100, block, stardump::kLineBytes) == 0 &&
                   stardump::formatWords(hex, sizeof(hex), fl, sizeof(fl), block, stardump::kLineBytes + 4) == 0 &&
                   stardump::formatWords(hex, sizeof(hex), fl, sizeof(fl), block, 0) == 0,
               "a buffer too small, an oversize block and an empty one return nothing rather than overrunning");
        // the widest text a block can make: every word the most negative normal float, and every word a NaN
        unsigned char wide[stardump::kLineBytes];
        for (uint32_t i = 0; i < sizeof(wide); i += 4) { wide[i] = 0xFF; wide[i + 1] = 0xFF; wide[i + 2] = 0x7F; wide[i + 3] = 0xFF; }
        stardump::formatWords(hex, sizeof(hex), fl, sizeof(fl), wide, stardump::kLineBytes);
        expect(std::strlen(hex) <= stardump::kHexChars && std::strlen(fl) <= stardump::kFloatChars && std::strlen(fl) >= 32 * 12,
               "the widest block's hex and float text fit the sizes the runtime declares (" + std::to_string(std::strlen(hex)) + " + " +
                   std::to_string(std::strlen(fl)) + ")");
        unsigned char nans[stardump::kLineBytes];
        std::memset(nans, 0xFF, sizeof(nans));
        stardump::formatWords(hex, sizeof(hex), fl, sizeof(fl), nans, stardump::kLineBytes);
        expect(std::strlen(fl) <= stardump::kFloatChars, "a block of NaNs fits too");
        expect(stardump::dataLines(0) == 0 && stardump::dataLines(16) == 1 && stardump::dataLines(128) == 1 && stardump::dataLines(129) == 2 &&
                   stardump::dataLines(1024) == 8 && stardump::dataLines(65536) == 8 && stardump::dumpBytes(65536) == 1024 &&
                   stardump::dumpBytes(256) == 256,
               "a buffer is dumped to at most 1024 bytes, in lines of 128");
        char list[24];
        const void* bound[8] = {&one, nullptr, &one, &one, nullptr, nullptr, nullptr, &one};
        stardump::slotList(bound, 8, list, sizeof(list));
        expect(std::strcmp(list, "0,2,3,7") == 0, "the bound slots are listed");
        const void* none[8] = {};
        stardump::slotList(none, 8, list, sizeof(list));
        expect(std::strcmp(list, "-") == 0, "and none is a dash");
    }

    // ===== 3. the budget =====================================================================================================================
    {
        stardump::Census c;
        uint32_t taken = 0;
        for (uint32_t i = 0; i < 50; ++i) taken += c.take() ? 1 : 0;
        expect(taken == 32 && c.dumped == 32 && c.seen == 50 && c.skipped == 18, "of 50 star draws, 32 are dumped and 18 are counted as skipped");
        stardump::Census few;
        for (uint32_t i = 0; i < 3; ++i) few.take();
        expect(few.dumped == 3 && few.skipped == 0, "a census with fewer than 32 skips none");
        stardump::SeenSet seen;
        expect(seen.find(0xAB, 64) == nullptr, "nothing is seen at first");
        seen.add(0xAB, 64, 7, 1, false);
        const stardump::Seen* hit = seen.find(0xAB, 64);
        expect(hit && hit->q == 7 && hit->slot == 1 && !hit->pixel && seen.find(0xAB, 80) == nullptr && seen.find(0xAC, 64) == nullptr,
               "a buffer already dumped is found by its hash and its size, and only by both");
        for (uint32_t i = 0; i < stardump::SeenSet::kMax + 10; ++i) seen.add(0x1000 + i, 16, i, 0, true);
        expect(seen.n == stardump::SeenSet::kMax, "the seen set stops at its capacity rather than overrunning");
    }

    // ===== 4. the log-line gate reaches the dump =============================================================================================
    {
        const std::string cpp = slurp("src/d3d11/flat_star_dump.cpp");
        const std::vector<flat_log_lines::NoteCall> calls = flat_log_lines::scanNotes(cpp, "src/d3d11/flat_star_dump.cpp");
        size_t flatNamed = 0;
        const flat_log_lines::NoteCall* data = nullptr;
        for (const auto& call : calls) {
            if (call.literal && call.format.rfind("flat star", 0) == 0) ++flatNamed;
            if (call.literal && call.format.rfind("flat star cb data:", 0) == 0) data = &call;
            expect(flat_log_lines::inScopeFile(call.file), "the dump's notes are in the log-line gate's scope by file name");
        }
        expect(calls.size() >= 8 && flatNamed == calls.size(), "every note the dump writes begins 'flat star' (" + std::to_string(calls.size()) + " calls)");
        expect(data != nullptr, "the data line is found in the source");
        if (data) {
            const flat_log_lines::Estimate w20 = flat_log_lines::estimateCall(*data, 20, nullptr, {}, 0);
            expect(!w20.unclassified && w20.length >= 600 && w20.length <= flat_log_lines::kFlatLogLineBudget,
                   "the data line's worst case (both buffers counted at their declared sizes) is " + std::to_string(w20.length) + " characters, under " +
                       std::to_string(flat_log_lines::kFlatLogLineBudget));
        }
        for (const auto& call : calls) {
            const flat_log_lines::Estimate w12 = flat_log_lines::estimateCall(call, 12, nullptr, {}, 0);
            expect(!w12.unclassified && w12.length <= flat_log_lines::kLoggerCut, "a star dump line can reach " + std::to_string(w12.length) + " characters at 12 digits: " + call.format.substr(0, 40));
        }
    }

    // ===== 5. the wiring =====================================================================================================================
    {
        const std::string cpp = slurp("src/d3d11/flat_star_dump.cpp"), hdr = slurp("src/d3d11/flat_star_dump.h");
        const std::string census = slurp("src/d3d11/draw_census.cpp"), vscreen = slurp("src/d3d11/vscreen.cpp");
        const std::string runtime = slurp("src/d3d11/flat_runtime.cpp"), build = slurp("build.bat");
        const std::string journal = slurp("src/d3d11/journal_watch.cpp");

        // the never-ran signature: a begin and an end in every flat census, with or without a star
        const std::string begin = between(cpp, "void flatStarDumpBegin(", "void flatStarDumpCapMissed()");
        expect(!begin.empty() && begin.find("if (!runtimeFlatProfile()) return;") != std::string::npos &&
                   begin.find("flat star dump: begin census=") != std::string::npos && begin.find("noteTruth(census)") != std::string::npos &&
                   begin.find("if (!runtimeFlatProfile()) return;") < begin.find("flat star dump: begin census="),
               "a flat census writes the begin line and the journal's truth whether or not any star draw follows, and a VR census writes nothing");
        const std::string end = between(cpp, "void flatStarDumpEnd(", "}  // namespace edvr");
        expect(!end.empty() && end.find("if (!g_active) return;") != std::string::npos && end.find("flat star dump: end census=") != std::string::npos &&
                   end.find("star-draws=%u dumped=%u skipped=%u cap-missed=%u faults=%u") != std::string::npos,
               "the end line is written for every census that began, with the counts that tell a failed dump from an empty one");
        expect(census.find("flatStarDumpBegin(g_censusNo, g_framesWanted, frameNo);") != std::string::npos &&
                   census.find("flatStarDumpBegin(") > census.find("\"DC begin census=%u"),
               "the census begin calls the dump's begin right after its own begin line");
        const std::string finish = between(census, "void finish() {", "}  // namespace");
        expect(finish.find("flatStarDumpEnd(g_censusNo);") != std::string::npos && finish.find("flatStarDumpEnd(g_censusNo);") > finish.find("DC end census=%u"),
               "the census end calls the dump's end after its own end line");

        // the draw hook: the shadow's pair, a capped census counts what it kept out, the dump follows the census line
        const std::string record = between(census, "static void recordDraw(", "void drawCensusEyeDraw(");
        const size_t sel = record.find("stardump::solarPair(starVs, starPs)"), cap = record.find("flatStarDumpCapMissed()"),
                     line = record.find("s2, s3, tail, xt, pt, vsr, vt, sv, q);"), dump = record.find("flatStarDumpDraw(ctx, sf);");
        expect(sel != std::string::npos && cap != std::string::npos && line != std::string::npos && dump != std::string::npos &&
                   sel < record.find("if (g_lines >= g_maxLines)") && cap < record.find("++g_lines;") && line < dump,
               "recordDraw selects on the binding shadow's pair before the line cap, counts a capped star draw, and dumps after the census line");
        expect(record.find("sf.q = q;") != std::string::npos && record.find("sf.index = index;") != std::string::npos &&
                   record.find("sf.frame = g_frameOrdinal;") != std::string::npos && record.find("sf.tag = tag;") != std::string::npos,
               "the dump is handed the census's own tag, frame, index and q, so it lines up with the DC/DCO line");
        expect(hdr.find("kMaxDraws = 32") != std::string::npos && cpp.find("g_census.take()") != std::string::npos,
               "the draw budget is 32 a census (one Home press) and is taken once for each selected draw");

        // a bound buffer is never left out of the dump: one the accessor refused is still logged, unshadowed, with its size
        expect(cpp.find("if (!bound[slot]) continue;") != std::string::npos && cpp.find("if (!info.bound) {") != std::string::npos &&
                   cpp.find("GetDesc(&bd);\n                info.buffer = bound[slot];") != std::string::npos &&
                   cpp.find("dumpBuffer(f, pixel, slot, info, bytes);") != std::string::npos && cpp.find("if (info.bound) dumpBuffer") == std::string::npos &&
                   cpp.find("unshadowed=%s") != std::string::npos,
               "every slot the context holds a buffer in is logged: shadowed with its bytes, or unshadowed with the reason and the buffer's size");

        // the first sight
        const std::string hook = between(vscreen, "static void flatCensusDraw(", "// Built v14.34");
        expect(hook.find("flatStarFirstSightDone()") != std::string::npos && hook.find("self == g_state->ownerCtx") != std::string::npos &&
                   hook.find("flatStarDumpNoteDraw(") != std::string::npos && hook.find("flatStarDumpNoteDraw(") < hook.find("drawCensusOffDraw("),
               "the flat draw hook looks for the first solar draw on the owner context, and costs a flag load once it has fired");
        expect(cpp.find("g_firstSightDone.exchange(true") != std::string::npos && cpp.find("flat star first sight:") != std::string::npos,
               "the first sight is written once");

        // observe-only: nothing on the GPU or on the context's state
        const char* const forbidden[] = {"->Map(", "->Unmap(", "CopyResource", "CopySubresourceRegion", "UpdateSubresource", "Flush(", "CreateBuffer(",
                                         "CreateQuery", "->Draw", "->Dispatch", "->OMSet", "->VSSet", "->PSSet", "->IASet", "->RSSet", "->CSSet",
                                         "->Clear", "->GenerateMips", "->ResolveSubresource", "->ExecuteCommandList", "->Begin(", "->End("};
        const std::string accessor = between(runtime, "void flatRuntimeObserveConstants(", "// The trace window for the frame that starts now");
        expect(!accessor.empty(), "the shadow accessor is found in flat_runtime.cpp");
        for (const char* word : forbidden) {
            expect(cpp.find(word) == std::string::npos, std::string("the dump does not call ") + word);
            expect(accessor.find(word) == std::string::npos, std::string("the shadow accessor does not call ") + word);
        }
        expect(accessor.find("copyConstants(") != std::string::npos && accessor.find("constantsMetadata(") != std::string::npos &&
                   accessor.find("ClearState") == std::string::npos,
               "the accessor reads the CPU shadow through copyConstants and constantsMetadata");

        // the journal's line parser gives the published answers nothing and takes nothing from them
        const std::string lines = between(journal, "// ---- The star facts: whole lines", "// Everything that is due, once.");
        expect(!lines.empty() && lines.find("s.pub.") == std::string::npos && lines.find("g_v.") == std::string::npos,
               "the star line parser neither writes nor reads the published answers");
        expect(journal.find("scanEvents(s, buf, got);\n            scanLines(s, buf, got);") != std::string::npos,
               "it runs beside the event scanner on the same read, after it");

        // the build compiles the module
        expect(build.find("\"src\\d3d11\\flat_star_dump.cpp\"") != std::string::npos, "build.bat compiles flat_star_dump.cpp into the DLL");
    }

    if (!failures) std::puts("  ok    star dump: selection, hex lines and their budget, 32 draws a census, the never-ran signature, and observe-only wiring");
    return failures;
}

}  // namespace flat_star_dump_rig

inline int flatStarDumpTests() { return flat_star_dump_rig::run(); }
