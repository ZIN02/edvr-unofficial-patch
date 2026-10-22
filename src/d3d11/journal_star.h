#pragma once

// The star facts the game's journal states outright, read for logging only.
//
// Two events carry them. StartJump with JumpType Hyperspace names the star class of the system the
// jump is headed for ("StarClass"); a Scan whose body has a StarType describes one star of the system
// the player is in (its class, subclass, luminosity, mass, radius, absolute magnitude, surface
// temperature and distance from the arrival point). The star dump (flat_star_dump.h) prints the
// current system's table in its header, so each dump carries the game's own account of the star it
// was taken at.
//
// Pure: a line of text in, a table out, no file, thread or log in this header. journal_watch.cpp
// assembles whole lines from its chunked reads and feeds them in; the gate rig feeds lines in
// directly. Nothing here changes what any consumer of the journal watcher is told.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace journalstar {

// A journal line longer than this is not parsed (a star Scan with its rings is well under 4 KB).
constexpr uint32_t kLineCap = 8192;
// The stars kept for the current system; a system with more than this counts the rest as dropped.
constexpr uint32_t kMaxStars = 16;
// Body and system names are kept to this many bytes, including the terminator.
constexpr uint32_t kNameCap = 64;

enum : uint32_t {
    kHaveSubclass = 1u << 0,
    kHaveLuminosity = 1u << 1,
    kHaveMass = 1u << 2,
    kHaveRadius = 1u << 3,
    kHaveAbsMag = 1u << 4,
    kHaveTemp = 1u << 5,
    kHaveDist = 1u << 6,
    kHaveScanType = 1u << 7,
    kHaveSystem = 1u << 8,
};

struct Jump {
    bool valid = false;     // a hyperspace StartJump has been read
    bool arrived = false;   // and the FSDJump that resolves it (the jump class is then the system the player is in)
    char system[kNameCap] = {};
    char starClass[12] = {};
};

struct Star {
    char body[kNameCap] = {};
    char system[kNameCap] = {};
    char type[12] = {};          // StarType: M, G, O, DA, N, H, TTS ...
    char luminosity[12] = {};    // Va, IV ...
    char scanType[24] = {};
    int32_t subclass = 0;
    double mass = 0, radius = 0, absMag = 0, temp = 0, dist = 0;   // solar masses, metres, magnitude, kelvin, light seconds
    uint32_t have = 0;           // which of the fields above were in the line
};

enum class Outcome : uint8_t {
    Ignored,        // not a line this module reads
    Malformed,      // an event it reads, without the fields that make it usable
    JumpHyperspace, // a hyperspace StartJump: the jump class is replaced and the stars are cleared
    JumpOther,      // a StartJump that is not hyperspace (supercruise): nothing changes
    Arrived,        // FSDJump: the stars of the system left behind are cleared
    StarAdded,
    StarRepeat,     // a body already in the table: not added, not logged
    StarFull,       // the table is full: counted
};

struct Table {
    Jump jump;
    char arrivedSystem[kNameCap] = {};   // the StarSystem of the last FSDJump: where the player is
    uint32_t jumps = 0;      // hyperspace StartJumps read
    uint32_t arrivals = 0;   // FSDJumps read
    Star stars[kMaxStars];
    uint32_t count = 0;      // stars in the table
    uint32_t repeats = 0;    // Scans of a body already in it
    uint32_t dropped = 0;    // stars the full table could not hold

    void clearStars() {
        count = 0;
        repeats = 0;
        dropped = 0;
    }
    void clearAll() { *this = Table(); }
};

// ---- a small reader for the one-line JSON objects the journal writes -----------------------------------------------------------------

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// The value of `"key":` as a position in the line, or -1. A match must be a whole quoted key, followed by a colon, and not
// itself inside a string (a quote preceded by a backslash).
inline long keyValueAt(const char* line, size_t n, const char* key) {
    const size_t k = std::strlen(key);
    for (size_t i = 0; i + k + 2 < n; ++i) {
        if (line[i] != '"' || (i && line[i - 1] == '\\')) continue;
        if (std::memcmp(line + i + 1, key, k) != 0 || line[i + 1 + k] != '"') continue;
        size_t j = i + k + 2;
        while (j < n && isSpace(line[j])) ++j;
        if (j >= n || line[j] != ':') continue;
        ++j;
        while (j < n && isSpace(line[j])) ++j;
        if (j < n) return static_cast<long>(j);
    }
    return -1;
}

// A string value, copied (truncated to cap-1) with its escapes taken as the character they escape. False when the key is
// absent or its value is not a string.
inline bool jsonString(const char* line, size_t n, const char* key, char* out, size_t cap) {
    if (out && cap) out[0] = '\0';
    const long at = keyValueAt(line, n, key);
    if (at < 0 || line[at] != '"' || !out || !cap) return false;
    size_t o = 0;
    size_t i = static_cast<size_t>(at) + 1;
    for (; i < n && line[i] != '"'; ++i) {
        char c = line[i];
        if (c == '\\' && i + 1 < n) c = line[++i];
        if (o + 1 < cap) out[o++] = c;
    }
    out[o] = '\0';
    return i < n;   // closed by its quote
}

// A number: sign, digits, an optional fraction and exponent. Not strtod, whose decimal point follows the process locale.
inline bool parseNumber(const char* p, const char* end, double* out) {
    static const double kPow10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    bool neg = false;
    if (p < end && (*p == '-' || *p == '+')) neg = *p++ == '-';
    uint64_t mant = 0;
    int digits = 0, scale = 0, any = 0;
    for (; p < end && *p >= '0' && *p <= '9'; ++p) {
        any = 1;
        if (digits < 18) { mant = mant * 10 + static_cast<uint64_t>(*p - '0'); if (mant) ++digits; }
        else ++scale;
    }
    if (p < end && *p == '.') {
        for (++p; p < end && *p >= '0' && *p <= '9'; ++p) {
            any = 1;
            if (digits < 18) { mant = mant * 10 + static_cast<uint64_t>(*p - '0'); if (mant) ++digits; --scale; }
        }
    }
    if (!any) return false;
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        bool eneg = false;
        if (p < end && (*p == '-' || *p == '+')) eneg = *p++ == '-';
        int e = 0;
        for (; p < end && *p >= '0' && *p <= '9'; ++p) if (e < 1000) e = e * 10 + (*p - '0');
        scale += eneg ? -e : e;
    }
    double v = static_cast<double>(mant);
    while (scale > 0) { const int s = scale > 22 ? 22 : scale; v *= kPow10[s]; scale -= s; if (v > 1e300) break; }
    while (scale < 0) { const int s = -scale > 22 ? 22 : -scale; v /= kPow10[s]; scale += s; if (v < 1e-300) break; }
    *out = neg ? -v : v;
    return true;
}

inline bool jsonNumber(const char* line, size_t n, const char* key, double* out) {
    const long at = keyValueAt(line, n, key);
    if (at < 0) return false;
    return parseNumber(line + at, line + n, out);
}

inline bool stringIs(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

// ---- the two events -----------------------------------------------------------------------------------------------------------------

// A hyperspace StartJump. False for any other line, and for a StartJump whose JumpType is not Hyperspace.
inline bool parseHyperspaceJump(const char* line, size_t n, Jump* out) {
    char type[24];
    if (!jsonString(line, n, "JumpType", type, sizeof(type)) || !stringIs(type, "Hyperspace")) return false;
    Jump j;
    j.valid = true;
    jsonString(line, n, "StarSystem", j.system, sizeof(j.system));
    jsonString(line, n, "StarClass", j.starClass, sizeof(j.starClass));
    *out = j;
    return true;
}

// A Scan of a star: the body has a StarType. False for a planet, a ring, a belt cluster, or a line without a body name.
inline bool parseStarScan(const char* line, size_t n, Star* out) {
    Star s;
    if (!jsonString(line, n, "StarType", s.type, sizeof(s.type)) || !s.type[0]) return false;
    if (!jsonString(line, n, "BodyName", s.body, sizeof(s.body)) || !s.body[0]) return false;
    if (jsonString(line, n, "StarSystem", s.system, sizeof(s.system))) s.have |= kHaveSystem;
    if (jsonString(line, n, "Luminosity", s.luminosity, sizeof(s.luminosity))) s.have |= kHaveLuminosity;
    if (jsonString(line, n, "ScanType", s.scanType, sizeof(s.scanType))) s.have |= kHaveScanType;
    double v = 0;
    if (jsonNumber(line, n, "Subclass", &v)) { s.subclass = static_cast<int32_t>(v); s.have |= kHaveSubclass; }
    if (jsonNumber(line, n, "StellarMass", &s.mass)) s.have |= kHaveMass;
    if (jsonNumber(line, n, "Radius", &s.radius)) s.have |= kHaveRadius;
    if (jsonNumber(line, n, "AbsoluteMagnitude", &s.absMag)) s.have |= kHaveAbsMag;
    if (jsonNumber(line, n, "SurfaceTemperature", &s.temp)) s.have |= kHaveTemp;
    if (jsonNumber(line, n, "DistanceFromArrivalLS", &s.dist)) s.have |= kHaveDist;
    *out = s;
    return true;
}

// One whole journal line into the table. `star` / `jump` receive what the outcome names, for the caller's log line.
inline Outcome applyLine(Table& t, const char* line, size_t n, Star* star, Jump* jump) {
    char ev[24];
    if (n < 12 || !jsonString(line, n, "event", ev, sizeof(ev))) return Outcome::Ignored;
    if (stringIs(ev, "StartJump")) {
        Jump j;
        if (!parseHyperspaceJump(line, n, &j)) return Outcome::JumpOther;
        t.jump = j;
        ++t.jumps;
        t.clearStars();
        if (jump) *jump = j;
        return Outcome::JumpHyperspace;
    }
    if (stringIs(ev, "FSDJump")) {
        ++t.arrivals;
        jsonString(line, n, "StarSystem", t.arrivedSystem, sizeof(t.arrivedSystem));
        // The jump class is the system the player is in only when the arrival is the system the StartJump named.
        t.jump.arrived = t.jump.valid && t.arrivedSystem[0] && stringIs(t.jump.system, t.arrivedSystem);
        t.clearStars();
        if (jump) {
            // What the arrival says: the system, and the class only when the StartJump named this system.
            Jump a;
            a.valid = true;
            a.arrived = t.jump.arrived;
            std::memcpy(a.system, t.arrivedSystem, sizeof(a.system));
            if (t.jump.arrived) std::memcpy(a.starClass, t.jump.starClass, sizeof(a.starClass));
            *jump = a;
        }
        return Outcome::Arrived;
    }
    if (stringIs(ev, "Scan")) {
        Star s;
        if (!parseStarScan(line, n, &s)) {
            // A Scan of a planet, ring or belt is not this module's business; only a line that names a StarType and not a body is.
            char probe[12];
            return jsonString(line, n, "StarType", probe, sizeof(probe)) ? Outcome::Malformed : Outcome::Ignored;
        }
        for (uint32_t i = 0; i < t.count; ++i)
            if (stringIs(t.stars[i].body, s.body)) { ++t.repeats; return Outcome::StarRepeat; }
        if (t.count >= kMaxStars) { ++t.dropped; return Outcome::StarFull; }
        t.stars[t.count++] = s;
        if (star) *star = s;
        return Outcome::StarAdded;
    }
    return Outcome::Ignored;
}

// ---- the text of a row ---------------------------------------------------------------------------------------------------------------

inline int putNumber(char* out, size_t cap, const char* name, bool have, double v) {
    if (have) return std::snprintf(out, cap, " %s=%.9g", name, v);
    return std::snprintf(out, cap, " %s=-", name);
}

// body="..." system="..." type=M sub=3 lum=Va mass=0.832031 radius=619543200 absmag=6.00116 temp=4846 dist-ls=0 scan=AutoScan
// Longest: two 63-character names, short words and five numbers of at most 17 characters each: under 400.
inline int formatStar(char* out, size_t cap, const Star& s) {
    if (!out || cap < 8) return -1;
    size_t at = 0;
    auto add = [&](int n) { if (n > 0) at += static_cast<size_t>(n) < cap - at ? static_cast<size_t>(n) : cap - at - 1; };
    add(std::snprintf(out + at, cap - at, "body=\"%s\" system=\"%s\" type=%s", s.body, (s.have & kHaveSystem) ? s.system : "-", s.type));
    if (s.have & kHaveSubclass) add(std::snprintf(out + at, cap - at, " sub=%d", static_cast<int>(s.subclass)));
    else add(std::snprintf(out + at, cap - at, " sub=-"));
    add(std::snprintf(out + at, cap - at, " lum=%s", (s.have & kHaveLuminosity) ? s.luminosity : "-"));
    add(putNumber(out + at, cap - at, "mass", (s.have & kHaveMass) != 0, s.mass));
    add(putNumber(out + at, cap - at, "radius", (s.have & kHaveRadius) != 0, s.radius));
    add(putNumber(out + at, cap - at, "absmag", (s.have & kHaveAbsMag) != 0, s.absMag));
    add(putNumber(out + at, cap - at, "temp", (s.have & kHaveTemp) != 0, s.temp));
    add(putNumber(out + at, cap - at, "dist-ls", (s.have & kHaveDist) != 0, s.dist));
    add(std::snprintf(out + at, cap - at, " scan=%s", (s.have & kHaveScanType) ? s.scanType : "-"));
    return static_cast<int>(at);
}

// system="..." class=M arrived=0
inline int formatJump(char* out, size_t cap, const Jump& j) {
    if (!out || cap < 8) return -1;
    return std::snprintf(out, cap, "system=\"%s\" class=%s arrived=%d", j.system, j.starClass[0] ? j.starClass : "-", j.arrived ? 1 : 0);
}

}  // namespace journalstar
}  // namespace edvr
