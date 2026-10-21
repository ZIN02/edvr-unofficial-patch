// Two read-only views of the flat runtime for the optimized station's sun-skip probe (station_sun_probe.h; built 2026-10-21, TEMPORARY with
// diag.station_sun_skip). Log-only: neither changes a decision, and both answer false anywhere but the flat runtime's owner thread, so the
// runtime's state is never read across threads.
#pragma once

#include <cstdint>

namespace edvr {

// The flat runtime's selected conflict for a frame, as its `flat runtime conflict` line reports it.
struct FlatStationProbeConflict {
    uint64_t frame = 0;
    const char* cause = "";    // the runtime's own static name for the cause
    const void* hdr = nullptr; // `hdr=`: the HDR target's texture
    uint32_t refQ = 0, curQ = 0;
};

// The flat runtime's frame in progress (the number its `flat runtime conflict` line prints) and its draw sequence now: the q of the last draw its
// scope numbered this frame. A draw skipped before the scope has no q of its own; it sits after this one. False off the owner thread, with the
// runtime inactive, or while its work is paused.
bool flatRuntimeStationProbeSequence(uint64_t* frame, uint32_t* q);

// The conflict the runtime reported for `frame` (the frame in which its image copy was judged), kept past the Present that clears the frame's
// state. False when it reported none for that frame, or off the owner thread.
bool flatRuntimeStationProbeConflict(uint64_t frame, FlatStationProbeConflict* out);

}  // namespace edvr
