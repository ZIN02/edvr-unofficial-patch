#pragma once
// THE HISTORY DEPTH CHECK ON THE CPU (src/d3d11/flat_mono_shader_source.h, historyDepthMatches; design doc sections 82 and 104).
//
// The check decides whether last frame's depth confirms that a pixel's history shows the surface the pixel shows now. This is the same decision in
// float32, statement for statement, so a case that is too large for the GPU rig (a 960x600 frame under the shipped jitter cycle) can run here, and so
// the GPU rig (tools\flat_mono_resolve_test\flat_taa_history_depth_gpu_tests.h) can compare the kernel with it pixel for pixel. tools\flat_temporal_test
// pins the HLSL's text to this; the GPU rig pins its numbers.
//
// Rule (the shader's comment above the function has the reasoning):
//   1. The point test: the best of the four history-depth texels around the reprojected raster position is within tol of `expected`,
//      tol = max(floor, expected * rel). This is what the check was.
//   2. When it refuses, the footprint test: the four texels all lie inside the image, are finite and non-zero, are planar (the cross difference
//      |d00 + d11 - d10 - d01| is within tol) and their bilinear value at the exact position is within tol of `expected`.
//
// A Rule with fields changed is a mutant of the rule; the tests hold each one to failing.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "../../src/common/temporal_math.h"

namespace histdepth {

constexpr float kRel = 0.01f;     // flat_mono_refusal.h: kFlatMonoStaleDepthRelative
constexpr float kFloor = 1e-6f;   // flat_mono_refusal.h: kFlatMonoStaleDepthFloor

struct Image {
    int w = 0, h = 0;
    std::vector<float> d;
    Image() = default;
    Image(int width, int height) : w(width), h(height), d(size_t(width) * size_t(height), 0.0f) {}
    float& at(int x, int y) { return d[size_t(y) * size_t(w) + size_t(x)]; }
    // HistoryDepth.Load(clamp(...)): a coordinate outside the image reads the edge texel.
    float load(int x, int y) const {
        x = std::min(std::max(x, 0), w - 1);
        y = std::min(std::max(y, 0), h - 1);
        return d[size_t(y) * size_t(w) + size_t(x)];
    }
};

struct Rule {
    float rel = kRel, floor = kFloor;
    bool footprint = true;        // false: the point test alone, the check as it was before the footprint test
    bool planar = true;           // false: the cross difference is not asked
    float planarScale = 1.0f;     // the cross difference's tolerance, in tol
    bool exactPosition = true;    // false: the mean of the four texels stands for their value at the exact position
    bool swappedWeights = false;  // true: the horizontal fraction weights the rows and the vertical one the columns
    bool wholeFootprint = true;   // false: a footprint partly outside the image is asked, its edge texels clamped
    bool solid = true;            // false: a zero (sky, cleared) or non-finite texel does not bar the footprint
};

inline Rule pointOnly() { Rule r; r.footprint = false; return r; }

struct Verdict {
    bool kept = false;
    bool byFootprint = false;     // kept by the footprint test after the point test refused
    bool inside = false;          // the 2x2 footprint lies wholly inside the image
    bool solid = false;           // ... and all four texels are finite and non-zero
    float margin = 1e30f;         // the smallest relative distance of a compared value from its threshold (an arithmetic ulp's worth of safety)
    float lowest = 0, highest = 0;   // the smallest and the largest of the four texels around the position (edge texels repeated)
};

// prevU, prevV: the camera term's previous position in [0,1] (unjittered); jx, jy: the previous frame's raster phase in render pixels.
inline Verdict decide(const Image& hist, float prevU, float prevV, float jx, float jy, float expected, const Rule& rule = Rule()) {
    Verdict v;
    const float rx = prevU * float(hist.w) + jx - 0.5f, ry = prevV * float(hist.h) + jy - 0.5f;
    const int bx = int(std::floor(rx)), by = int(std::floor(ry));
    const float tol = std::max(rule.floor, expected * rule.rel);
    const auto note = [&](float value, float threshold) {
        const float t = std::max(std::fabs(threshold), 1e-30f);
        v.margin = std::min(v.margin, std::fabs(value - threshold) / t);
    };
    float best = 3.402823e38f;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) best = std::min(best, std::fabs(hist.load(bx + x, by + y) - expected));
    note(best, tol);
    v.inside = bx >= 0 && by >= 0 && bx < hist.w - 1 && by < hist.h - 1;
    {
        const float a = hist.load(bx, by), b = hist.load(bx + 1, by), c = hist.load(bx, by + 1), e = hist.load(bx + 1, by + 1);
        v.lowest = std::min(std::min(a, b), std::min(c, e));
        v.highest = std::max(std::max(a, b), std::max(c, e));
        v.solid = v.inside && std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && std::isfinite(e) && a > 0 && b > 0 && c > 0 && e > 0;
    }
    if (best <= tol) { v.kept = true; return v; }
    if (!rule.footprint) return v;
    if (rule.wholeFootprint && !v.inside) return v;
    const float d00 = hist.load(bx, by), d10 = hist.load(bx + 1, by), d01 = hist.load(bx, by + 1), d11 = hist.load(bx + 1, by + 1);
    if (rule.solid) {
        const bool finite = std::isfinite(d00) && std::isfinite(d10) && std::isfinite(d01) && std::isfinite(d11);
        if (!finite || !(d00 > 0 && d10 > 0 && d01 > 0 && d11 > 0)) return v;
    }
    const float fx = rx - float(bx), fy = ry - float(by);
    const float wx = rule.swappedWeights ? fy : fx, wy = rule.swappedWeights ? fx : fy;
    const float top = d00 + wx * (d10 - d00), bottom = d01 + wx * (d11 - d01);
    const float value = rule.exactPosition ? top + wy * (bottom - top) : 0.25f * (d00 + d10 + d01 + d11);
    note(std::fabs(value - expected), tol);
    const float cross = std::fabs(d00 + d11 - d10 - d01);
    if (rule.planar) note(cross, tol * rule.planarScale);
    const bool depthOk = std::fabs(value - expected) <= tol;
    const bool planarOk = !rule.planar || cross <= tol * rule.planarScale;
    if (depthOk && planarOk) { v.kept = true; v.byFootprint = true; }
    return v;
}

// The depth the footprint holds at the exact position: what the footprint test compares with `expected`. Tests build a surface "whose depth is
// the interpolation" from it.
inline float bilinearAt(const Image& hist, float prevU, float prevV, float jx, float jy) {
    const float rx = prevU * float(hist.w) + jx - 0.5f, ry = prevV * float(hist.h) + jy - 0.5f;
    const int bx = int(std::floor(rx)), by = int(std::floor(ry));
    const float fx = rx - float(bx), fy = ry - float(by);
    const float d00 = hist.load(bx, by), d10 = hist.load(bx + 1, by), d01 = hist.load(bx, by + 1), d11 = hist.load(bx + 1, by + 1);
    const float top = d00 + fx * (d10 - d00), bottom = d01 + fx * (d11 - d01);
    return top + fy * (bottom - top);
}


// ---- the GPU rig's scenes ---------------------------------------------------------------------------------------------------------------------
// tools\flat_mono_resolve_test runs the taa kernel on a 16 x 16 frame whose current phase is 0 (so the colour a pixel samples is exactly its texel's) and
// whose previous phase is (px, py). A scene here is the pair of depth images that rig is handed, plus what this model says each pixel does; the rig
// compares the kernel with it, and tools\flat_temporal_test holds each scene to what it is for.
namespace rig16 {
constexpr int N = 16;

struct Scene {
    const char* name;
    std::vector<float> before, now;   // last frame's depth (rendered at phase (px, py)) and this frame's (phase 0), row-major
    float px, py;
};

// A surface is the depth it has at a true (unjittered) position, 0 for sky. A texel (x, y) rendered at phase (jx, jy) holds the surface at (x + .5 - jx, y + .5 - jy).
// floorAt falls 5% per pixel at the top row to 2.8% at the bottom one (d = .01 * (1 + y / 20): 1 / (20 + y) of itself per row) and has no slope in x;
// tiltAt also falls along x.
inline float floorAt(float, float yt) { return 0.01f * (1.0f + yt / 20.0f); }
inline float tiltAt(float xt, float yt) { return 0.01f * (1.0f + (yt + 0.5f * xt) / 20.0f); }

template <class F>
inline std::vector<float> render(F surface, float jx, float jy) {
    std::vector<float> z(size_t(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) z[size_t(y) * N + x] = surface(float(x) + 0.5f - jx, float(y) + 0.5f - jy);
    return z;
}
inline Image image(const std::vector<float>& z) {
    Image m(N, N);
    m.d = z;
    return m;
}

// A still plane: the same surface now and last frame.
inline Scene slope(const char* name, float px, float py, bool tilted) {
    Scene s{name, {}, {}, px, py};
    if (tilted) { s.before = render(tiltAt, px, py); s.now = render(tiltAt, 0, 0); }
    else { s.before = render(floorAt, px, py); s.now = render(floorAt, 0, 0); }
    return s;
}
// The plane last frame was `factor` times as deep (2% a frame is a surface moving 2% of its range between frames).
inline Scene mover(const char* name, float px, float py, float factor) {
    Scene s{name, {}, {}, px, py};
    s.before = render(floorAt, px, py);
    for (float& v : s.before) v *= factor;
    s.now = render(floorAt, 0, 0);
    return s;
}
// A near object last frame (columns 5..9, rows 5..9), the floor behind it now.
inline Scene object(const char* name, float px, float py) {
    Scene s{name, {}, {}, px, py};
    s.before = render(floorAt, px, py);
    for (int y = 5; y <= 9; ++y)
        for (int x = 5; x <= 9; ++x) s.before[size_t(y) * N + x] = 0.04f;
    s.now = render(floorAt, 0, 0);
    return s;
}
// A sloped roof against the sky whose edge was 1.5 pixels farther right last frame.
inline Scene skyEdge(const char* name, float px, float py) {
    Scene s{name, {}, {}, px, py};
    const auto roofNow = [](float xt, float yt) { return xt >= 6.0f ? floorAt(xt, yt) : 0.0f; };
    const auto roofThen = [](float xt, float yt) { return xt >= 7.5f ? floorAt(xt, yt) : 0.0f; };
    s.before = render(roofThen, px, py);
    s.now = render(roofNow, 0, 0);
    return s;
}
// A plane that reaches depth 0 exactly on the centre of texel row 7 last frame (its horizon), the sky above it: the zero texel is consistent with the plane,
// and the footprints that hold it are still not asked.
inline Scene horizon(const char* name, float px, float py) {
    Scene s{name, {}, {}, px, py};
    const float h0 = 7.5f - py;   // texel row 7, rendered at phase py, is centred on true row 7.5 - py
    const auto plane = [h0](float, float yt) { return yt > h0 ? 0.01f * (yt - h0) / 20.0f : 0.0f; };
    s.before = render(plane, px, py);
    s.now = render(plane, 0, 0);
    return s;
}
// A checker of two depths 3% apart last frame, and a surface this frame whose depth at each pixel is the checker's interpolation at the exact previous position.
inline Scene interpolated(const char* name, float px, float py) {
    Scene s{name, {}, {}, px, py};
    s.before.resize(size_t(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) s.before[size_t(y) * N + x] = ((x + y) & 1) ? 0.0103f : 0.0100f;
    const Image m = image(s.before);
    s.now.resize(size_t(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) s.now[size_t(y) * N + x] = bilinearAt(m, (float(x) + 0.5f) / N, (float(y) + 0.5f) / N, px, py);
    return s;
}

// The cases the GPU rig runs, by kind and by the index of the previous frame's phase in the 32-phase cycle (temporalJitter(n, count 32)): the phases
// are the shipped Halton points, picked where the point test alone refuses most of the scene and the previous phase is not within a few hundredths of
// a pixel of a texel centre (where one ulp of the camera term's arithmetic could move the footprint).
//   f a floor (no slope in x), t a tilted plane, m a floor 2% deeper last frame, o a near object that moved away, e a roof against the sky,
//   c a checker whose interpolation the surface is, h a horizon that ends on a texel row.
struct Plan { char kind; unsigned n; };
inline const Plan* plan(size_t* count) {
    static const Plan p[] = {{'f', 2}, {'f', 7}, {'f', 4}, {'f', 5}, {'t', 6}, {'m', 1}, {'m', 28}, {'o', 2}, {'e', 7},
                             {'c', 9}, {'c', 10}, {'h', 2}, {'h', 4}};
    *count = sizeof(p) / sizeof(p[0]);
    return p;
}
inline const char* kindName(char k) {
    switch (k) {
        case 'f': return "a still floor";
        case 't': return "a still tilted plane";
        case 'm': return "a floor 2% deeper last frame";
        case 'o': return "a near object that moved away";
        case 'e': return "a roof against the sky";
        case 'c': return "a surface that is a checker's interpolation";
        case 'h': return "a horizon on a texel row";
        default: return "?";
    }
}
constexpr unsigned kCycle = 32;
inline void phaseOf(unsigned n, float* x, float* y) { edvr::temporalJitter(n % kCycle, x, y, kCycle); }
inline Scene build(const Plan& p) {
    float px = 0, py = 0;
    phaseOf(p.n, &px, &py);
    const char* name = kindName(p.kind);
    switch (p.kind) {
        case 'f': return slope(name, px, py, false);
        case 't': return slope(name, px, py, true);
        case 'm': return mover(name, px, py, 1.02f);
        case 'o': return object(name, px, py);
        case 'e': return skyEdge(name, px, py);
        case 'c': return interpolated(name, px, py);
        default: return horizon(name, px, py);
    }
}

// What `rule` says of every pixel of a scene (the camera is still, so a pixel's previous position is itself and its expected depth is its own).
inline std::vector<Verdict> predict(const Scene& s, const Rule& rule) {
    const Image m = image(s.before);
    std::vector<Verdict> v(size_t(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            v[size_t(y) * N + x] = decide(m, (float(x) + 0.5f) / N, (float(y) + 0.5f) / N, s.px, s.py, s.now[size_t(y) * N + x], rule);
    return v;
}
}  // namespace rig16

}  // namespace histdepth
