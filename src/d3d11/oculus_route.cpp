#include "oculus_route.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

#include "../common/elite_oculus_profile.h"

#include "../common/log.h"
#include "../common/proxy.h"

namespace edvr {
namespace {

using LoadLibraryWFn = HMODULE(WINAPI*)(LPCWSTR);

constexpr uint32_t kStatusVersion = 1;
constexpr uint32_t kDecisionNone = 0;
constexpr uint32_t kDecisionForwarded = 1;
constexpr uint32_t kDecisionRejected = 2;
constexpr uint32_t kDecisionUnknownCaller = 3;
constexpr uint32_t kDecisionMalformedName = 4;
constexpr uint32_t kDecisionNoOriginal = 5;
constexpr uint32_t kDecisionNotNative = 6;
constexpr uint32_t kDecisionInstallFailure = 7;
constexpr uint32_t kMaxBoundedWideName = 1024;
constexpr uint32_t kMaxReportSnapshots = 32;

// All fields used by the wrapper are fixed-size atomics.  In particular, no
// std::string, CRT locale helper, or lazy singleton is reachable from it.
std::atomic<uint32_t> g_nativeBuild{0};
std::atomic<uint32_t> g_capabilitySet{0};
std::atomic<uint32_t> g_profileKnown{0};
std::atomic<uint32_t> g_profileFailureStage{0};
std::atomic<uint32_t> g_slotValidated{0};
std::atomic<uint32_t> g_callerValidated{0};
std::atomic<uint32_t> g_installed{0};
std::atomic<uint32_t> g_installing{0};
// Published before the IAT CAS.  A thread entering through the newly
// exchanged slot must filter immediately; waiting for a later bookkeeping
// flag would create a race in which the first LibOVR attempt is forwarded.
std::atomic<uint32_t> g_routingReady{0};
std::atomic<uint32_t> g_originalPublished{0};
std::atomic<uint32_t> g_uninstallOwned{0};

std::atomic<void**> g_iatSlot{nullptr};
std::atomic<void*> g_original{nullptr};
std::atomic<uintptr_t> g_executableBase{0};
std::atomic<uintptr_t> g_callerReturnRva{0};
// Temporary instrument for the 334473 route bypass (docs/build-334473.md); remove when that arc closes.
std::atomic<int64_t> g_installQpc{0};

std::atomic<uint32_t> g_installAttempts{0};
std::atomic<uint32_t> g_installFailures{0};
std::atomic<uint32_t> g_calls{0};
std::atomic<uint32_t> g_exactName{0};
std::atomic<uint32_t> g_rejected{0};
std::atomic<uint32_t> g_forwarded{0};
std::atomic<uint32_t> g_unknownCaller{0};
std::atomic<uint32_t> g_nearName{0};
std::atomic<uint32_t> g_malformedName{0};
std::atomic<uint32_t> g_noOriginal{0};
std::atomic<uint32_t> g_publicationRaces{0};
std::atomic<uint32_t> g_capabilityMismatches{0};
std::atomic<uint32_t> g_reportReady{0};
std::atomic<uint32_t> g_callsBeforeReportReady{0};
std::atomic<uint32_t> g_callsAfterReportReady{0};
std::atomic<uint32_t> g_lastDecision{kDecisionNone};
std::atomic<uint32_t> g_lastError{0};

std::atomic<uint32_t> g_reportCount{0};
std::atomic<uint32_t> g_reportLimitReached{0};
std::atomic<uint32_t> g_reportedCalls{0};
std::atomic<uint32_t> g_reportedRejected{0};
std::atomic<uint32_t> g_reportedForwarded{0};
std::atomic<uint32_t> g_reportedExact{0};
std::atomic<uint32_t> g_reportedUnknown{0};
std::atomic<uint32_t> g_reportedMalformed{0};
std::atomic<uint32_t> g_reportedInstalled{0};
std::atomic<uint32_t> g_reportInitialized{0};
std::atomic<uint32_t> g_criticalRejectionReported{0};
std::atomic<uint32_t> g_reporting{0};
std::atomic<uint32_t> g_eliteProcess{0};

// These two values are written before publishing g_installed and are only
// read for a status snapshot or after the slot has been restored.  The atomics
// also make the race visible to the test seam and avoid a torn pointer read.
std::atomic<uintptr_t> g_lastCallerReturnRva{0};

template <typename T>
void saturatingIncrement(std::atomic<T>& value) {
    // Counters are evidence, not control flow.  One lock-free increment keeps
    // the hook bounded even if a hostile caller hammers it.  Saturation is
    // restored after the single fetch-add's only possible wrap at UINT_MAX.
    const T before = value.fetch_add(static_cast<T>(1), std::memory_order_relaxed);
    if (before == static_cast<T>(~static_cast<T>(0))) {
        value.store(before, std::memory_order_relaxed);
    }
}

#if defined(EDVR_OCULUS_ROUTE_TEST)
std::atomic<uint32_t> g_testEliteProcessConfigured{0};
std::atomic<uint32_t> g_testEliteProcess{0};
std::atomic<void**> g_testProfileSlot{nullptr};
std::atomic<uintptr_t> g_testProfileCaller{0};
std::atomic<uint32_t> g_testProfileKnown{0};
std::atomic<uint32_t> g_testProfileConfigured{0};
std::atomic<OculusRouteTestAfterCas> g_testAfterCas{nullptr};
std::atomic<OculusRouteTestBeforeCas> g_testBeforeCas{nullptr};
std::atomic<OculusRouteTestReportSink> g_testReportSink{nullptr};
#endif

#if !defined(EDVR_OCULUS_ROUTE_TEST)
// Temporary instrument for the 334473 route bypass (docs/build-334473.md); remove when that arc closes.
// The watch thread writes only these atomics.  Log lines are written by
// slotWatchReport(), from oculusRouteReport(), outside the loader lock.
constexpr uint32_t kSlotWatchNoRoute = 1;
constexpr uint32_t kSlotWatchNoThread = 2;
constexpr int64_t kSlotWatchSeconds = 120;
constexpr size_t kSlotNameChars = 512;
constexpr size_t kSlotDescChars = 768;

std::atomic<uint32_t> g_slotWatchEntered{0};
std::atomic<uint32_t> g_slotWatchStartDone{0};
std::atomic<uint32_t> g_slotWatchStarted{0};
std::atomic<uint32_t> g_slotWatchFailure{0};
std::atomic<uint32_t> g_slotWatchThreadError{0};
std::atomic<int64_t> g_slotWatchFreq{0};
std::atomic<int64_t> g_slotWatchStartQpc{0};
std::atomic<void*> g_slotWatchInitial{nullptr};
std::atomic<uint32_t> g_slotWatchChanges{0};
std::atomic<int64_t> g_slotWatchFirstChangeQpc{0};
std::atomic<void*> g_slotWatchFirstChangeValue{nullptr};
std::atomic<int64_t> g_slotWatchLatestChangeQpc{0};
std::atomic<void*> g_slotWatchLatestChangeValue{nullptr};
std::atomic<int64_t> g_slotLibOvrQpc{0};
std::atomic<void*> g_slotLibOvrValue{nullptr};
std::atomic<int64_t> g_slotOpenVrQpc{0};
std::atomic<void*> g_slotOpenVrValue{nullptr};
std::atomic<uint32_t> g_slotWatchFinished{0};
std::atomic<int64_t> g_slotWatchEndQpc{0};
std::atomic<uint32_t> g_slotLoggedStart{0};
std::atomic<uint32_t> g_slotLoggedChange{0};
std::atomic<uint32_t> g_slotLoggedLibOvr{0};
std::atomic<uint32_t> g_slotLoggedOpenVr{0};
std::atomic<uint32_t> g_slotLoggedEnd{0};
#endif

bool validateProfile(void* executable, OculusProfileMatch* match) {
#if defined(EDVR_OCULUS_ROUTE_TEST)
    if (g_testProfileConfigured.load(std::memory_order_acquire)) {
        match->loadLibrarySlot = g_testProfileSlot.load(std::memory_order_acquire);
        match->callerReturnRva = g_testProfileCaller.load(std::memory_order_acquire);
        match->failureStage = static_cast<uint32_t>(
            OculusProfileFailureStage::Header);
        if (g_testProfileKnown.load(std::memory_order_acquire)) {
            const bool valid = match->loadLibrarySlot != nullptr && match->callerReturnRva != 0;
            if (valid) match->failureStage = static_cast<uint32_t>(OculusProfileFailureStage::None);
            return valid;
        }
        return false;
    }
#endif
    return eliteOculusProfileValidate(executable, match);
}

void setDecision(uint32_t decision, DWORD error = ERROR_SUCCESS) {
    g_lastError.store(static_cast<uint32_t>(error), std::memory_order_relaxed);
    g_lastDecision.store(decision, std::memory_order_relaxed);
}

bool isEliteProcess() noexcept {
    wchar_t path[32768]{};
    const DWORD n=GetModuleFileNameW(nullptr,path,_countof(path));
    if(!n||n>=_countof(path))return false;
    const wchar_t* leaf=path;
    for(DWORD i=0;i<n;++i)if(path[i]==L'\\'||path[i]==L'/')leaf=path+i+1;
    constexpr wchar_t name[]=L"EliteDangerous64.exe";
    for(size_t i=0;name[i];++i) {
        wchar_t a=leaf[i],b=name[i];
        if(!a)return false;
        if(a>=L'a'&&a<=L'z')a=static_cast<wchar_t>(a-L'a'+L'A');
        if(b>=L'a'&&b<=L'z')b=static_cast<wchar_t>(b-L'a'+L'A');
        if(a!=b)return false;
    }
    return leaf[_countof(name)-1]==L'\0';
}

// Reads at most 1024 UTF-16 code units under SEH.  A caller can hand the
// wrapper a dangling pointer while a module is unloading; that is a forward,
// not a reason to let EDVR become the crash site.
bool basenameInfo(LPCWSTR path, bool* exact, bool* nearName, bool* malformed) {
    *exact = false;
    *nearName = false;
    *malformed = false;
    if (!path) {
        *malformed = true;
        return false;
    }

    wchar_t leaf[kMaxBoundedWideName] = {};
    uint32_t length = 0;
    bool terminated = false;
    __try {
        for (; length + 1 < kMaxBoundedWideName; ++length) {
            const wchar_t c = path[length];
            if (c == L'\0') {
                terminated = true;
                break;
            }
            leaf[length] = c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *malformed = true;
        return false;
    }
    if (!terminated) {
        *malformed = true;
        return false;
    }

    uint32_t start = 0;
    for (uint32_t i = 0; i < length; ++i) {
        if (leaf[i] == L'\\' || leaf[i] == L'/') start = i + 1;
    }
    const wchar_t* name = leaf + start;
    constexpr wchar_t kTarget[] = L"LibOVRRT64_1.dll";
    uint32_t nameLength = length - start;
    uint32_t targetLength = static_cast<uint32_t>(sizeof(kTarget) / sizeof(kTarget[0]) - 1);
    if (nameLength == targetLength) {
        bool same = true;
        for (uint32_t i = 0; i < targetLength; ++i) {
            wchar_t a = name[i];
            wchar_t b = kTarget[i];
            if (a >= L'a' && a <= L'z') a = static_cast<wchar_t>(a - L'a' + L'A');
            if (b >= L'a' && b <= L'z') b = static_cast<wchar_t>(b - L'a' + L'A');
            if (a != b) {
                same = false;
                break;
            }
        }
        *exact = same;
    }
    // Diagnostic classification only.  It never changes the forwarding
    // decision: exact basename plus exact caller is the complete policy.
    if (!*exact && nameLength >= 6) {
        bool hasOvr = false;
        constexpr wchar_t kPrefix[] = L"LIBOVR";
        for (uint32_t i = 0; i + 6 <= nameLength; ++i) {
            bool same = true;
            for (uint32_t j = 0; j < 6; ++j) {
                wchar_t c = name[i + j];
                if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
                if (c != kPrefix[j]) {
                    same = false;
                    break;
                }
            }
            if (same) {
                hasOvr = true;
                break;
            }
        }
        *nearName = hasOvr;
    }
    return true;
}

bool callerMatches(uintptr_t returnAddress) {
    const uintptr_t base = g_executableBase.load(std::memory_order_acquire);
    const uintptr_t expected = g_callerReturnRva.load(std::memory_order_acquire);
    if (!base || !expected || returnAddress < base) return false;
    return returnAddress - base == expected;
}

HMODULE routeDispatch(LPCWSTR path, uintptr_t returnAddress,
                      LoadLibraryWFn injectedOriginal = nullptr) {
    const DWORD incomingError = GetLastError();
    saturatingIncrement(g_calls);
    if (g_reportReady.load(std::memory_order_acquire)) {
        saturatingIncrement(g_callsAfterReportReady);
    } else {
        saturatingIncrement(g_callsBeforeReportReady);
    }
    g_lastCallerReturnRva.store(returnAddress >= g_executableBase.load(std::memory_order_relaxed)
                                    ? returnAddress - g_executableBase.load(std::memory_order_relaxed)
                                    : 0,
                                std::memory_order_relaxed);

    LoadLibraryWFn original = injectedOriginal;
    if (!original) {
        original = reinterpret_cast<LoadLibraryWFn>(g_original.load(std::memory_order_acquire));
    }
    if (!original) {
        saturatingIncrement(g_noOriginal);
        setDecision(kDecisionNoOriginal, ERROR_MOD_NOT_FOUND);
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }

    if (!g_nativeBuild.load(std::memory_order_acquire) || !g_routingReady.load(std::memory_order_acquire)) {
        saturatingIncrement(g_forwarded);
        setDecision(kDecisionNotNative);
        SetLastError(incomingError);
        HMODULE result = original(path);
        const DWORD originalError = GetLastError();
        setDecision(kDecisionNotNative, originalError);
        SetLastError(originalError);
        return result;
    }
    if (!callerMatches(returnAddress)) {
        saturatingIncrement(g_unknownCaller);
        saturatingIncrement(g_forwarded);
        setDecision(kDecisionUnknownCaller);
        SetLastError(incomingError);
        HMODULE result = original(path);
        const DWORD originalError = GetLastError();
        setDecision(kDecisionUnknownCaller, originalError);
        SetLastError(originalError);
        return result;
    }

    bool exact = false;
    bool nearName = false;
    bool malformed = false;
    basenameInfo(path, &exact, &nearName, &malformed);
    if (malformed) {
        saturatingIncrement(g_malformedName);
        saturatingIncrement(g_forwarded);
        setDecision(kDecisionMalformedName);
        SetLastError(incomingError);
        HMODULE result = original(path);
        const DWORD originalError = GetLastError();
        setDecision(kDecisionMalformedName, originalError);
        SetLastError(originalError);
        return result;
    }
    if (nearName) saturatingIncrement(g_nearName);
    if (!exact) {
        saturatingIncrement(g_forwarded);
        setDecision(kDecisionForwarded);
        SetLastError(incomingError);
        HMODULE result = original(path);
        const DWORD originalError = GetLastError();
        setDecision(kDecisionForwarded, originalError);
        SetLastError(originalError);
        return result;
    }

    saturatingIncrement(g_exactName);
    saturatingIncrement(g_rejected);
    setDecision(kDecisionRejected, ERROR_MOD_NOT_FOUND);
    SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
}

HMODULE WINAPI oculusRouteLoadLibraryW(LPCWSTR path) {
    return routeDispatch(path, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

#if !defined(EDVR_OCULUS_ROUTE_TEST)
// Temporary instrument for the 334473 route bypass (docs/build-334473.md); remove when that arc closes.
int64_t slotWatchQpc() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// A plain word read.  volatile keeps each read of the IAT slot in the watch loop.
void* readSlot(void** slot) {
    return *static_cast<void* volatile*>(slot);
}

double qpcMs(int64_t from, int64_t to, int64_t freq) {
    if (freq <= 0) return 0.0;
    return static_cast<double>(to - from) * 1000.0 / static_cast<double>(freq);
}

bool claimOnce(std::atomic<uint32_t>& flag) {
    return flag.load(std::memory_order_relaxed) == 0 &&
           flag.exchange(1, std::memory_order_acq_rel) == 0;
}

// Base and basename of the module containing address.  The no-refcount lookup
// loads nothing.
bool moduleOwning(const void* address, uintptr_t* base, char* name, size_t nameSize) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module)) {
        return false;
    }
    *base = reinterpret_cast<uintptr_t>(module);
    wchar_t path[1024] = {};
    const DWORD length = GetModuleFileNameW(module, path, 1024);
    const wchar_t* leaf = L"unknown module";
    if (length != 0 && length < 1024) {
        leaf = path;
        for (const wchar_t* p = path; *p; ++p) {
            if (*p == L'\\' || *p == L'/') leaf = p + 1;
        }
    }
    if (WideCharToMultiByte(CP_UTF8, 0, leaf, -1, name, static_cast<int>(nameSize),
                            nullptr, nullptr) == 0) {
        std::snprintf(name, nameSize, "unknown module");
    }
    return true;
}

// Names a LoadLibraryW slot value for the log.  Nothing is loaded.
void describeSlot(void* value, char* out, size_t outSize) {
    if (value == reinterpret_cast<void*>(&oculusRouteLoadLibraryW)) {
        std::snprintf(out, outSize, "EDVR's route");
        return;
    }
    const uintptr_t address = reinterpret_cast<uintptr_t>(value);
    char module[kSlotNameChars];
    uintptr_t base = 0;
    const bool found = address != 0 && moduleOwning(value, &base, module, sizeof(module));
    void* const original = g_original.load(std::memory_order_acquire);
    if (original && value == original) {
        std::snprintf(out, outSize, "the original LoadLibraryW EDVR saved at install (%s)",
                      found ? module : "no loaded module");
    } else if (found) {
        std::snprintf(out, outSize, "0x%llX in %s+0x%llX",
                      static_cast<unsigned long long>(address), module,
                      static_cast<unsigned long long>(address - base));
    } else {
        std::snprintf(out, outSize, "0x%llX in no loaded module",
                      static_cast<unsigned long long>(address));
    }
}

// Samples the slot every 1 ms for 120 s and checks the two runtime modules
// every 10 ms.  It logs nothing: slotWatchReport() reads the atomics.
DWORD WINAPI slotWatchThreadMain(LPVOID) {
    const int64_t freq = g_slotWatchFreq.load(std::memory_order_acquire);
    const int64_t start = g_slotWatchStartQpc.load(std::memory_order_acquire);
    const int64_t end = start + freq * kSlotWatchSeconds;
    const int64_t moduleStep = freq / 100;
    void** const slot = g_iatSlot.load(std::memory_order_acquire);
    void* previous = g_slotWatchInitial.load(std::memory_order_acquire);
    uint32_t changes = 0;
    int64_t nextModuleCheck = start;
    int64_t now = start;
    for (;;) {
        Sleep(1);
        now = slotWatchQpc();
        void* const value = readSlot(slot);
        if (value != previous) {
            previous = value;
            if (changes == 0) {
                g_slotWatchFirstChangeQpc.store(now, std::memory_order_relaxed);
                g_slotWatchFirstChangeValue.store(value, std::memory_order_relaxed);
            }
            g_slotWatchLatestChangeQpc.store(now, std::memory_order_relaxed);
            g_slotWatchLatestChangeValue.store(value, std::memory_order_relaxed);
            ++changes;
            g_slotWatchChanges.store(changes, std::memory_order_release);
        }
        if (now >= nextModuleCheck) {
            nextModuleCheck = now + moduleStep;
            if (g_slotLibOvrQpc.load(std::memory_order_relaxed) == 0 &&
                GetModuleHandleW(L"LibOVRRT64_1.dll")) {
                g_slotLibOvrValue.store(value, std::memory_order_relaxed);
                g_slotLibOvrQpc.store(now, std::memory_order_release);
            }
            if (g_slotOpenVrQpc.load(std::memory_order_relaxed) == 0 &&
                GetModuleHandleW(L"openvr_api.dll")) {
                g_slotOpenVrValue.store(value, std::memory_order_relaxed);
                g_slotOpenVrQpc.store(now, std::memory_order_release);
            }
        }
        if (now >= end) break;
    }
    g_slotWatchEndQpc.store(now, std::memory_order_relaxed);
    g_slotWatchFinished.store(1, std::memory_order_release);
    return 0;
}

// Emits the slot-watch lines, each once, from oculusRouteReport().  Nothing
// here runs under the loader lock.
void slotWatchReport() {
    if (!g_slotWatchStartDone.load(std::memory_order_acquire)) return;
    const bool started = g_slotWatchStarted.load(std::memory_order_acquire) != 0;
    const int64_t freq = g_slotWatchFreq.load(std::memory_order_relaxed);
    const int64_t startQpc = g_slotWatchStartQpc.load(std::memory_order_relaxed);
    if (claimOnce(g_slotLoggedStart)) {
        if (started) {
            char desc[kSlotDescChars];
            describeSlot(g_slotWatchInitial.load(std::memory_order_relaxed), desc, sizeof(desc));
            const uintptr_t slot = reinterpret_cast<uintptr_t>(g_iatSlot.load(std::memory_order_acquire));
            const uintptr_t executable = g_executableBase.load(std::memory_order_acquire);
            Log::get().note("oculus route slot: at log open (%.1f ms after the route installed) the LoadLibraryW import slot exe+0x%X holds %s; watching it every 1 ms for 120 s.",
                            qpcMs(g_installQpc.load(std::memory_order_acquire), startQpc, freq),
                            static_cast<unsigned>(slot - executable), desc);
        } else if (g_slotWatchFailure.load(std::memory_order_relaxed) == kSlotWatchNoRoute) {
            Log::get().note("oculus route slot: watch not started: route not installed.");
        } else {
            Log::get().note("oculus route slot: watch not started: thread could not start (CreateThread error %u).",
                            g_slotWatchThreadError.load(std::memory_order_relaxed));
        }
    }
    if (!started) return;

    const uint32_t changes = g_slotWatchChanges.load(std::memory_order_acquire);
    if (changes != 0 && claimOnce(g_slotLoggedChange)) {
        char desc[kSlotDescChars];
        describeSlot(g_slotWatchFirstChangeValue.load(std::memory_order_relaxed), desc, sizeof(desc));
        Log::get().note("oculus route slot: changed %.1f ms after log open: it now holds %s (%u change(s) so far).",
                        qpcMs(startQpc, g_slotWatchFirstChangeQpc.load(std::memory_order_relaxed), freq),
                        desc, changes);
    }

    const int64_t libOvrQpc = g_slotLibOvrQpc.load(std::memory_order_acquire);
    if (libOvrQpc != 0 && claimOnce(g_slotLoggedLibOvr)) {
        char desc[kSlotDescChars];
        describeSlot(g_slotLibOvrValue.load(std::memory_order_relaxed), desc, sizeof(desc));
        Log::get().note("oculus route slot: LibOVRRT64_1.dll appeared %.1f ms after log open; the slot then held %s.",
                        qpcMs(startQpc, libOvrQpc, freq), desc);
    }
    const int64_t openVrQpc = g_slotOpenVrQpc.load(std::memory_order_acquire);
    if (openVrQpc != 0 && claimOnce(g_slotLoggedOpenVr)) {
        char desc[kSlotDescChars];
        describeSlot(g_slotOpenVrValue.load(std::memory_order_relaxed), desc, sizeof(desc));
        Log::get().note("oculus route slot: openvr_api.dll appeared %.1f ms after log open; the slot then held %s.",
                        qpcMs(startQpc, openVrQpc, freq), desc);
    }

    if (g_slotWatchFinished.load(std::memory_order_acquire) && claimOnce(g_slotLoggedEnd)) {
        // The slot as the watch last saw it: the latest change, or the value at log open.
        const uint32_t total = g_slotWatchChanges.load(std::memory_order_acquire);
        void* const holds = total != 0 ? g_slotWatchLatestChangeValue.load(std::memory_order_relaxed)
                                       : g_slotWatchInitial.load(std::memory_order_relaxed);
        char desc[kSlotDescChars];
        describeSlot(holds, desc, sizeof(desc));
        Log::get().note("oculus route slot: watch ended after %.1f ms: %u change(s); the slot holds %s.",
                        qpcMs(startQpc, g_slotWatchEndQpc.load(std::memory_order_relaxed), freq),
                        total, desc);
    }
}
#endif

}  // namespace

HMODULE oculusRouteDispatchFrom(LPCWSTR path, uintptr_t returnAddress,
                                HMODULE (WINAPI* forward)(LPCWSTR)) {
    return routeDispatch(path, returnAddress, forward);
}

void oculusRouteInstallEarly(bool nativeBuild) {
    saturatingIncrement(g_installAttempts);
    if (nativeBuild) {
#if defined(EDVR_OCULUS_ROUTE_TEST)
        if (!g_testEliteProcessConfigured.load(std::memory_order_acquire))
#endif
            g_eliteProcess.store(isEliteProcess() ? 1u : 0u, std::memory_order_release);
    }
    if (g_installing.exchange(1, std::memory_order_acq_rel)) return;
    const auto releaseInstall = [] { g_installing.store(0, std::memory_order_release); };
    uint32_t expected = 0;
    if (!g_capabilitySet.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                                 std::memory_order_relaxed)) {
        if (g_nativeBuild.load(std::memory_order_acquire) != (nativeBuild ? 1u : 0u)) {
            saturatingIncrement(g_capabilityMismatches);
        }
    } else {
        g_nativeBuild.store(nativeBuild ? 1u : 0u, std::memory_order_release);
    }

    if (!g_nativeBuild.load(std::memory_order_acquire)) {
        setDecision(kDecisionNotNative);
        releaseInstall();
        return;
    }
    if (g_installed.load(std::memory_order_acquire)) {
        releaseInstall();
        return;
    }

    OculusProfileMatch match{};
    const bool known = validateProfile(GetModuleHandleW(nullptr), &match);
    g_profileKnown.store(known ? 1u : 0u, std::memory_order_release);
    g_profileFailureStage.store(match.failureStage, std::memory_order_release);
    if (!known || !match.loadLibrarySlot || !match.callerReturnRva) {
        saturatingIncrement(g_installFailures);
        if(g_eliteProcess.load(std::memory_order_acquire)) {
            OutputDebugStringA("EDVR Oculus route not installed: Elite Oculus profile/IAT hook unavailable\n");
        }
        setDecision(kDecisionInstallFailure);
        releaseInstall();
        return;
    }

    void* current = *match.loadLibrarySlot;
    if (!current || current == reinterpret_cast<void*>(&oculusRouteLoadLibraryW)) {
        saturatingIncrement(g_installFailures);
        if(g_eliteProcess.load(std::memory_order_acquire)) {
            OutputDebugStringA("EDVR Oculus route not installed: Elite Oculus IAT target unavailable\n");
        }
        setDecision(kDecisionInstallFailure);
        releaseInstall();
        return;
    }

    // Publish every identity value before the slot exchange.  A wrapper that
    // wins a concurrent call can therefore never observe a partially formed
    // original target or caller identity.
    g_iatSlot.store(match.loadLibrarySlot, std::memory_order_release);
    g_executableBase.store(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                           std::memory_order_release);
    g_callerReturnRva.store(match.callerReturnRva, std::memory_order_release);
    g_slotValidated.store(1, std::memory_order_release);
    g_callerValidated.store(1, std::memory_order_release);
    g_original.store(current, std::memory_order_release);
    g_originalPublished.store(1, std::memory_order_release);
    g_routingReady.store(1, std::memory_order_release);

    DWORD oldProtect = 0;
    if (!VirtualProtect(match.loadLibrarySlot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        saturatingIncrement(g_installFailures);
        if(g_eliteProcess.load(std::memory_order_acquire)) {
            OutputDebugStringA("EDVR Oculus route not installed: Elite Oculus IAT protection failed\n");
        }
        g_routingReady.store(0, std::memory_order_release);
        setDecision(kDecisionInstallFailure);
        releaseInstall();
        return;
    }
#if defined(EDVR_OCULUS_ROUTE_TEST)
    if (const OculusRouteTestBeforeCas callback =
            g_testBeforeCas.load(std::memory_order_acquire)) {
        callback();
    }
#endif
    const void* previous = InterlockedCompareExchangePointer(
        match.loadLibrarySlot, reinterpret_cast<void*>(&oculusRouteLoadLibraryW), current);
    DWORD ignored = 0;
    VirtualProtect(match.loadLibrarySlot, sizeof(void*), oldProtect, &ignored);
    if (previous != current) {
        g_routingReady.store(0, std::memory_order_release);
        saturatingIncrement(g_publicationRaces);
        saturatingIncrement(g_installFailures);
        if(g_eliteProcess.load(std::memory_order_acquire)) {
            OutputDebugStringA("EDVR Oculus route not installed: Elite Oculus IAT publication raced\n");
        }
        setDecision(kDecisionInstallFailure);
        releaseInstall();
        return;
    }
#if defined(EDVR_OCULUS_ROUTE_TEST)
    if (const OculusRouteTestAfterCas callback =
            g_testAfterCas.load(std::memory_order_acquire)) {
        callback();
    }
#endif
    g_uninstallOwned.store(1, std::memory_order_release);
    // Temporary instrument for the 334473 route bypass (docs/build-334473.md); remove when that arc closes.
    // One QPC read under the loader lock; nothing else is added there.
    LARGE_INTEGER installedAt{};
    QueryPerformanceCounter(&installedAt);
    g_installQpc.store(installedAt.QuadPart, std::memory_order_release);
    g_installed.store(1, std::memory_order_release);
    setDecision(kDecisionNone);
    releaseInstall();
}

void oculusRouteUninstallEarly() {
    void** slot = g_iatSlot.load(std::memory_order_acquire);
    void* original = g_original.load(std::memory_order_acquire);
    if (!slot || !original || !g_uninstallOwned.load(std::memory_order_acquire)) return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return;
    InterlockedCompareExchangePointer(slot, original, reinterpret_cast<void*>(&oculusRouteLoadLibraryW));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
    g_routingReady.store(0, std::memory_order_release);
    g_installed.store(0, std::memory_order_release);
    g_uninstallOwned.store(0, std::memory_order_release);
}

OculusRouteStatus oculusRouteStatusSnapshot() {
    OculusRouteStatus s{};
    s.version = kStatusVersion;
    s.nativeBuild = g_nativeBuild.load(std::memory_order_acquire);
    s.profileKnown = g_profileKnown.load(std::memory_order_acquire);
    s.profileFailureStage = g_profileFailureStage.load(std::memory_order_acquire);
    s.slotValidated = g_slotValidated.load(std::memory_order_acquire);
    s.callerValidated = g_callerValidated.load(std::memory_order_acquire);
    s.installed = g_installed.load(std::memory_order_acquire);
    s.originalPublished = g_originalPublished.load(std::memory_order_acquire);
    s.uninstallOwned = g_uninstallOwned.load(std::memory_order_acquire);
    s.installAttempts = g_installAttempts.load(std::memory_order_relaxed);
    s.installFailures = g_installFailures.load(std::memory_order_relaxed);
    s.calls = g_calls.load(std::memory_order_relaxed);
    s.exactName = g_exactName.load(std::memory_order_relaxed);
    s.rejected = g_rejected.load(std::memory_order_relaxed);
    s.forwarded = g_forwarded.load(std::memory_order_relaxed);
    s.unknownCaller = g_unknownCaller.load(std::memory_order_relaxed);
    s.nearName = g_nearName.load(std::memory_order_relaxed);
    s.malformedName = g_malformedName.load(std::memory_order_relaxed);
    s.noOriginal = g_noOriginal.load(std::memory_order_relaxed);
    s.publicationRaces = g_publicationRaces.load(std::memory_order_relaxed);
    s.capabilityMismatches = g_capabilityMismatches.load(std::memory_order_relaxed);
    s.reportReady = g_reportReady.load(std::memory_order_acquire);
    s.callsBeforeReportReady = g_callsBeforeReportReady.load(std::memory_order_relaxed);
    s.callsAfterReportReady = g_callsAfterReportReady.load(std::memory_order_relaxed);
    s.lastDecision = g_lastDecision.load(std::memory_order_relaxed);
    s.lastError = g_lastError.load(std::memory_order_relaxed);
    s.reportCount = g_reportCount.load(std::memory_order_relaxed);
    s.reportLimitReached = g_reportLimitReached.load(std::memory_order_relaxed);
    s.iatSlot = reinterpret_cast<uintptr_t>(g_iatSlot.load(std::memory_order_acquire));
    s.originalTarget = reinterpret_cast<uintptr_t>(g_original.load(std::memory_order_acquire));
    s.executableBase = g_executableBase.load(std::memory_order_acquire);
    s.callerReturnRva = g_callerReturnRva.load(std::memory_order_acquire);
    s.lastCallerReturnRva = g_lastCallerReturnRva.load(std::memory_order_relaxed);
    return s;
}

#if !defined(EDVR_OCULUS_ROUTE_TEST)
// Temporary instrument for the 334473 route bypass (docs/build-334473.md); remove when that arc closes.
void oculusRouteSlotWatchStart() {
    if (g_slotWatchEntered.exchange(1, std::memory_order_acq_rel)) return;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    g_slotWatchFreq.store(frequency.QuadPart, std::memory_order_relaxed);
    g_slotWatchStartQpc.store(slotWatchQpc(), std::memory_order_relaxed);
    void** const slot = g_iatSlot.load(std::memory_order_acquire);
    if (!g_installed.load(std::memory_order_acquire) || !slot) {
        g_slotWatchFailure.store(kSlotWatchNoRoute, std::memory_order_relaxed);
    } else {
        // Both values are published before CreateThread, which orders them for the thread.
        g_slotWatchInitial.store(readSlot(slot), std::memory_order_relaxed);
        const HANDLE thread = CreateThread(nullptr, 0, slotWatchThreadMain, nullptr, 0, nullptr);
        if (!thread) {
            g_slotWatchThreadError.store(GetLastError(), std::memory_order_relaxed);
            g_slotWatchFailure.store(kSlotWatchNoThread, std::memory_order_relaxed);
        } else {
            CloseHandle(thread);
            g_slotWatchStarted.store(1, std::memory_order_relaxed);
        }
    }
    // Last, so slotWatchReport() never sees a half-recorded start.
    g_slotWatchStartDone.store(1, std::memory_order_release);
}
#endif

void oculusRouteReport() {
    // The first call is the handoff from loader-safe startup accounting to
    // normal logging.  Set this before taking the snapshot so a concurrent
    // loader call is classified as after-report-ready only when it actually
    // happened during the normal phase.
#if defined(EDVR_OCULUS_ROUTE_TEST)
    const OculusRouteTestReportSink testSink =
        g_testReportSink.load(std::memory_order_acquire);
    if (!testSink && !Log::get().isOpen()) return;
#else
    if (!Log::get().isOpen()) return;
    // Temporary instrument for the 334473 route bypass (docs/build-334473.md); remove when that arc closes.
    slotWatchReport();
#endif
    if (g_reporting.exchange(1, std::memory_order_acq_rel)) return;
    g_reportReady.store(1, std::memory_order_release);
    OculusRouteStatus s = oculusRouteStatusSnapshot();
    // A Present can call this every frame.  Calls alone are deliberately not
    // a report change: a quiet frame must not produce a log line merely
    // because the call counter is a census.  A new decision or state change
    // is observable and is still bounded by kMaxReportSnapshots.
    const bool first = g_reportInitialized.exchange(1, std::memory_order_acq_rel) == 0;
    const bool changed = s.rejected != g_reportedRejected.load(std::memory_order_relaxed) ||
        s.forwarded != g_reportedForwarded.load(std::memory_order_relaxed) ||
        s.exactName != g_reportedExact.load(std::memory_order_relaxed) ||
        s.unknownCaller != g_reportedUnknown.load(std::memory_order_relaxed) ||
        s.malformedName != g_reportedMalformed.load(std::memory_order_relaxed) ||
        s.installed != g_reportedInstalled.load(std::memory_order_relaxed);
    if (!first && !changed) {
        g_reporting.store(0, std::memory_order_release);
        return;
    }
    const bool critical = s.rejected != g_reportedRejected.load(std::memory_order_relaxed) &&
                          !g_criticalRejectionReported.load(std::memory_order_relaxed);
    const uint32_t before = g_reportCount.load(std::memory_order_relaxed);
    if (before >= kMaxReportSnapshots) {
        g_reportLimitReached.store(1, std::memory_order_relaxed);
        s.reportLimitReached = 1;
    }
    if (before >= kMaxReportSnapshots && !critical) {
        g_reportedCalls.store(s.calls, std::memory_order_relaxed);
        g_reportedRejected.store(s.rejected, std::memory_order_relaxed);
        g_reportedForwarded.store(s.forwarded, std::memory_order_relaxed);
        g_reportedExact.store(s.exactName, std::memory_order_relaxed);
        g_reportedUnknown.store(s.unknownCaller, std::memory_order_relaxed);
        g_reportedMalformed.store(s.malformedName, std::memory_order_relaxed);
        g_reportedInstalled.store(s.installed, std::memory_order_relaxed);
        g_reporting.store(0, std::memory_order_release);
        return;
    }
    if (critical) g_criticalRejectionReported.store(1, std::memory_order_relaxed);
    if (before < kMaxReportSnapshots) g_reportCount.fetch_add(1, std::memory_order_relaxed);
    g_reportedCalls.store(s.calls, std::memory_order_relaxed);
    g_reportedRejected.store(s.rejected, std::memory_order_relaxed);
    g_reportedForwarded.store(s.forwarded, std::memory_order_relaxed);
    g_reportedExact.store(s.exactName, std::memory_order_relaxed);
    g_reportedUnknown.store(s.unknownCaller, std::memory_order_relaxed);
    g_reportedMalformed.store(s.malformedName, std::memory_order_relaxed);
    g_reportedInstalled.store(s.installed, std::memory_order_relaxed);
#if defined(EDVR_OCULUS_ROUTE_TEST)
    if (testSink) {
        testSink(&s);
    } else
#endif
    {
        Log::get().note("oculus route: native=%u profile=%u profile_failure=%u installed=%u calls=%u before_report=%u after_report=%u expected_caller_rva=0x%llX last_caller_rva=0x%llX exact=%u rejected=%u forwarded=%u unknown_caller=%u malformed=%u near=%u failures=%u publication_races=%u no_original=%u decision=%u error=%u report_limit=%u",
                        s.nativeBuild, s.profileKnown, s.profileFailureStage,
                        s.installed, s.calls,
                        s.callsBeforeReportReady, s.callsAfterReportReady,
                        static_cast<unsigned long long>(s.callerReturnRva),
                        static_cast<unsigned long long>(s.lastCallerReturnRva),
                        s.exactName, s.rejected, s.forwarded, s.unknownCaller,
                        s.malformedName, s.nearName, s.installFailures,
                        s.publicationRaces, s.noOriginal, s.lastDecision, s.lastError,
                        s.reportLimitReached);
#if !defined(EDVR_OCULUS_ROUTE_TEST)
        // The build string comes from proxy.cpp, which the route test rig does not link.
        if (first && g_eliteProcess.load(std::memory_order_acquire) && s.nativeBuild &&
            !s.installed && s.lastDecision == kDecisionInstallFailure) {
            const std::string build = gameBuildVersion();
            Log::get().note("oculus route: not installed on game build %s (profile known=%u, failure stage %u). The game starts normally. With the Oculus PC runtime installed, Elite may open it directly and bypass EDVR's OpenXR runtime until an EDVR update carries a profile for this build; SteamVR, other OpenXR runtimes and the flat profile are unaffected.",
                            build.empty() ? "unknown" : build.c_str(), s.profileKnown, s.profileFailureStage);
        }
#endif
    }
    g_reporting.store(0, std::memory_order_release);
}

#if defined(EDVR_OCULUS_ROUTE_TEST)
void oculusRouteTestReset() {
    oculusRouteUninstallEarly();
    g_nativeBuild.store(0, std::memory_order_relaxed);
    g_capabilitySet.store(0, std::memory_order_relaxed);
    g_profileKnown.store(0, std::memory_order_relaxed);
    g_profileFailureStage.store(0, std::memory_order_relaxed);
    g_slotValidated.store(0, std::memory_order_relaxed);
    g_callerValidated.store(0, std::memory_order_relaxed);
    g_installed.store(0, std::memory_order_relaxed);
    g_installing.store(0, std::memory_order_relaxed);
    g_routingReady.store(0, std::memory_order_relaxed);
    g_originalPublished.store(0, std::memory_order_relaxed);
    g_uninstallOwned.store(0, std::memory_order_relaxed);
    g_iatSlot.store(nullptr, std::memory_order_relaxed);
    g_original.store(nullptr, std::memory_order_relaxed);
    g_executableBase.store(0, std::memory_order_relaxed);
    g_callerReturnRva.store(0, std::memory_order_relaxed);
    g_installAttempts.store(0, std::memory_order_relaxed);
    g_installFailures.store(0, std::memory_order_relaxed);
    g_calls.store(0, std::memory_order_relaxed);
    g_exactName.store(0, std::memory_order_relaxed);
    g_rejected.store(0, std::memory_order_relaxed);
    g_forwarded.store(0, std::memory_order_relaxed);
    g_unknownCaller.store(0, std::memory_order_relaxed);
    g_nearName.store(0, std::memory_order_relaxed);
    g_malformedName.store(0, std::memory_order_relaxed);
    g_noOriginal.store(0, std::memory_order_relaxed);
    g_publicationRaces.store(0, std::memory_order_relaxed);
    g_capabilityMismatches.store(0, std::memory_order_relaxed);
    g_reportReady.store(0, std::memory_order_relaxed);
    g_callsBeforeReportReady.store(0, std::memory_order_relaxed);
    g_callsAfterReportReady.store(0, std::memory_order_relaxed);
    g_lastDecision.store(kDecisionNone, std::memory_order_relaxed);
    g_lastError.store(0, std::memory_order_relaxed);
    g_reportCount.store(0, std::memory_order_relaxed);
    g_reportLimitReached.store(0, std::memory_order_relaxed);
    g_reportedCalls.store(0, std::memory_order_relaxed);
    g_reportedRejected.store(0, std::memory_order_relaxed);
    g_reportedForwarded.store(0, std::memory_order_relaxed);
    g_reportedExact.store(0, std::memory_order_relaxed);
    g_reportedUnknown.store(0, std::memory_order_relaxed);
    g_reportedMalformed.store(0, std::memory_order_relaxed);
    g_reportedInstalled.store(0, std::memory_order_relaxed);
    g_reportInitialized.store(0, std::memory_order_relaxed);
    g_criticalRejectionReported.store(0, std::memory_order_relaxed);
    g_reporting.store(0, std::memory_order_relaxed);
    g_lastCallerReturnRva.store(0, std::memory_order_relaxed);
    g_eliteProcess.store(0, std::memory_order_relaxed);
#if defined(EDVR_OCULUS_ROUTE_TEST)
    g_testEliteProcessConfigured.store(0, std::memory_order_relaxed);
    g_testEliteProcess.store(0, std::memory_order_relaxed);
    g_testProfileSlot.store(nullptr, std::memory_order_relaxed);
    g_testProfileCaller.store(0, std::memory_order_relaxed);
    g_testProfileKnown.store(0, std::memory_order_relaxed);
    g_testProfileConfigured.store(0, std::memory_order_relaxed);
    g_testAfterCas.store(nullptr, std::memory_order_relaxed);
    g_testBeforeCas.store(nullptr, std::memory_order_relaxed);
    g_testReportSink.store(nullptr, std::memory_order_relaxed);
#endif
}

void oculusRouteTestSetProfile(void** slot, uintptr_t callerReturnRva, bool known) {
    g_testProfileSlot.store(slot, std::memory_order_release);
    g_testProfileCaller.store(callerReturnRva, std::memory_order_release);
    g_testProfileKnown.store(known ? 1u : 0u, std::memory_order_release);
    g_testProfileConfigured.store(1, std::memory_order_release);
}

void oculusRouteTestSetEliteProcess(bool elite) {
    g_testEliteProcess.store(elite ? 1u : 0u, std::memory_order_release);
    g_testEliteProcessConfigured.store(1, std::memory_order_release);
    g_eliteProcess.store(elite ? 1u : 0u, std::memory_order_release);
}

void oculusRouteTestSetAfterCas(OculusRouteTestAfterCas callback) {
    g_testAfterCas.store(callback, std::memory_order_release);
}

void oculusRouteTestSetBeforeCas(OculusRouteTestBeforeCas callback) {
    g_testBeforeCas.store(callback, std::memory_order_release);
}

void oculusRouteTestSetReportSink(OculusRouteTestReportSink sink) {
    g_testReportSink.store(sink, std::memory_order_release);
}

bool oculusRouteTestPublish(OculusRouteLoadLibraryW original,
                            uintptr_t executableBase,
                            uintptr_t callerReturnRva) {
    if (!original || !executableBase || !callerReturnRva) return false;
    oculusRouteTestReset();
    g_nativeBuild.store(1, std::memory_order_release);
    g_capabilitySet.store(1, std::memory_order_release);
    g_profileKnown.store(1, std::memory_order_release);
    g_slotValidated.store(1, std::memory_order_release);
    g_callerValidated.store(1, std::memory_order_release);
    g_executableBase.store(executableBase, std::memory_order_release);
    g_callerReturnRva.store(callerReturnRva, std::memory_order_release);
    g_original.store(reinterpret_cast<void*>(original), std::memory_order_release);
    g_originalPublished.store(1, std::memory_order_release);
    g_routingReady.store(1, std::memory_order_release);
    g_installed.store(1, std::memory_order_release);
    return true;
}

bool oculusRouteTestPublishSlot(void** slot, OculusRouteLoadLibraryW original,
                                uintptr_t executableBase,
                                uintptr_t callerReturnRva) {
    if (!slot || !*slot || !original || !executableBase || !callerReturnRva) return false;
    if (!oculusRouteTestPublish(original, executableBase, callerReturnRva)) return false;
    g_iatSlot.store(slot, std::memory_order_release);
    if (InterlockedCompareExchangePointer(
            slot, reinterpret_cast<void*>(&oculusRouteLoadLibraryW),
            reinterpret_cast<void*>(original)) != reinterpret_cast<void*>(original)) {
        g_iatSlot.store(nullptr, std::memory_order_release);
        g_installed.store(0, std::memory_order_release);
        return false;
    }
    g_uninstallOwned.store(1, std::memory_order_release);
    return true;
}

HMODULE oculusRouteTestDispatch(LPCWSTR path, uintptr_t callerReturnRva) {
    const uintptr_t base = g_executableBase.load(std::memory_order_acquire);
    return routeDispatch(path, base + callerReturnRva);
}
#endif

}  // namespace edvr
