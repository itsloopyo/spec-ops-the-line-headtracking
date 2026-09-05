#include "aim_trace.h"

#include "aim_marker.h"
#include "logging.h"
#include "ue3_types.h"

#include "minhook_util.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace SpecOpsTheLineHeadTracking {

namespace {

// UWorld::SingleLineCheck(FCheckResult& Hit, AActor* SourceActor, const FVector& End,
// const FVector& Start, DWORD TraceFlags, const FVector& Extent, DWORD) - seven stack
// arguments (the function ends `ret 0x1c`), `this` in ecx, returns non-zero when the
// line reached End without hitting anything. Reached from AActor::execFastTrace, which
// loads GWorld into ecx.
//
// The argument list was read off a live call rather than reconstructed from the call
// site: MSVC writes the outgoing arguments with movs into space reserved in the
// prologue, so the arity is not visible there, and a detour with the wrong arity
// corrupts the stack on return.
using SingleLineCheck_t = int(__fastcall*)(void* world, void* edx, void* hit,
                                           void* sourceActor, const UE3Vector* end,
                                           const UE3Vector* start, std::uint32_t flags,
                                           const UE3Vector* extent, std::uint32_t last);

SingleLineCheck_t g_lineCheck = nullptr;
void**            g_worldPtr = nullptr;
void*             g_original = nullptr;
void*             g_target = nullptr;

// Captured from the game's own crosshair line check rather than invented: reusing its
// source actor, trace flags and range is what makes this ray stop on the same surfaces
// the bullet does. Guessing a flag set instead is how a reticle ends up anchored to a
// trigger volume the player is standing in.
//
// Refreshed on EVERY frame's check, not latched on the first. The source actor is the
// one the trace must not hit - the player, whose own body is between a third-person
// camera and everything it is aimed at - and holding a pointer to it across a weapon
// swap, a cover transition or a level load is holding a pointer to a freed object.
// One publish, not four. The values are written on whichever engine thread ran the
// check and read on the game thread, so a flag set after four plain stores orders
// nothing: the reader could pair one capture's source actor with another's range, and
// nothing stops the compiler caching the actor pointer across the cast.
struct TraceParams {
    void*         sourceActor = nullptr;
    std::uint32_t flags = 0;
    std::uint32_t lastArg = 0;
    float         range = 0.0f;
};
std::atomic<bool> g_haveParams{false};
TraceParams       g_params;
std::mutex        g_paramsMutex;
// Set while our own ray is in flight, so the watcher does not treat it as the game's.
// Thread-local because that is the guard's actual scope: it marks a call this thread
// made, and the world runs line checks on more than one thread. A process-wide flag
// would both race and make one thread's cast swallow another thread's real check, which
// is the one the trace parameters have to be captured from.
thread_local bool g_inOurTrace = false;

// Restores the flag however the call leaves. Straight-line restore after a call into
// engine code loses it on an unwind, and the flag is load-bearing: left set, this
// thread's InspectTrace returns immediately for the rest of the session, the parameters
// freeze at whatever was captured last, and the mod keeps casting with them.
class OurTraceScope {
public:
    OurTraceScope() { g_inOurTrace = true; }
    ~OurTraceScope() { g_inOurTrace = false; }
    OurTraceScope(const OurTraceScope&) = delete;
    OurTraceScope& operator=(const OurTraceScope&) = delete;
};

// Cleared once a line check has been seen starting at the clean camera with a
// level-length range. That can only happen if the argument block is being decoded at the
// right offsets, so from then on the End/Start pointers are what the callee is about to
// dereference itself and the page check below stops running - see Readable.
std::atomic<bool> g_argsUnproven{true};

// Attempts spent looking for the game's own crosshair check before giving up. The
// Readable() guard below is two kernel transitions per line check and the world runs
// hundreds a frame, so bounding it by SUCCESS alone means a build whose crosshair ray
// does not start at the camera pays that rate for the whole session. Roughly a minute of
// gameplay at the measured 121 frames a second and a few hundred checks a frame.
constexpr unsigned long kMaxUnprovenChecks = 2000000;
std::atomic<unsigned long> g_unprovenChecks{0};
std::atomic<bool> g_gaveUp{false};

// Positions in the caller's argument block, in the order SingleLineCheck takes them:
// (FCheckResult*, AActor* SourceActor, const FVector* End, const FVector* Start,
// DWORD TraceFlags, const FVector* Extent, DWORD).
constexpr int kArgSourceActor = 1;
constexpr int kArgEnd = 2;
constexpr int kArgStart = 3;
constexpr int kArgTraceFlags = 4;
constexpr int kArgLast = 6;

// Read from the diagnostics accessors on another thread; see the note on the camera
// hook's counters.
std::atomic<unsigned long> g_matches{0};
std::atomic<unsigned long> g_hits{0};

// Offset of FCheckResult::Location, found once by looking for the three consecutive
// floats that land on the ray that was just cast. The layout does not change within a
// build, so this is resolved on the first hit and reused.
std::atomic<int> g_locationOffset{-1};
// Generous: the result is zero-initialised and only Location is read back, so no field
// beyond it has to be identified to use this safely.
constexpr int kCheckResultBytes = 0x100;

// Guards the FIRST dereferences of the caller's argument block, before anything has
// confirmed the arity above is right - a wrong one turns whatever sits at those stack
// slots into a pointer.
//
// It is not a permanent guard, because it must not be: VirtualQuery is a kernel
// transition and the world runs hundreds of line checks a frame, so paying two of them
// per check for the whole session costs more than everything else the mod does put
// together. Once a check has matched the camera the decode is proven, and Start and End
// are pointers SingleLineCheck itself is about to read.
bool Readable(const void* p, std::size_t bytes) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT) {
        return false;
    }
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & readable) == 0 || (mbi.Protect & PAGE_GUARD) != 0) {
        return false;
    }
    const auto* start = static_cast<const std::uint8_t*>(mbi.BaseAddress);
    return static_cast<const std::uint8_t*>(p) + bytes <= start + mbi.RegionSize;
}

// How close to the ray a triple of floats has to land to be the hit location. One
// engine unit is a centimetre, and nothing else in the result lands on the ray at all.
constexpr float kOnRayToleranceUnits = 1.0f;

// True when p lies on the segment from start along dir, within kOnRayToleranceUnits. A
// hit location does by construction; nothing else in the result does.
bool OnRay(const float* p, const UE3Vector& start, const UE3Vector& dir, float range) {
    const float dx = p[0] - start.X, dy = p[1] - start.Y, dz = p[2] - start.Z;
    const float t = dx * dir.X + dy * dir.Y + dz * dir.Z;
    if (!(t >= 0.0f) || t > range) {
        return false;
    }
    const float ex = dx - dir.X * t, ey = dy - dir.Y * t, ez = dz - dir.Z * t;
    return (ex * ex + ey * ey + ez * ez) < kOnRayToleranceUnits * kOnRayToleranceUnits;
}

// The world runs hundreds of line checks a frame. The crosshair's is the one that starts
// at the clean camera, which the camera hook publishes.
constexpr float kStartMatchUnits = 4.0f;

// Below this the check is a short local probe (a cover test, a footstep), not the ray the
// crosshair is resolved along, which runs to the far end of the level.
constexpr float kMinCameraRayUnits = 100.0f;

// FCheckResult is scanned a dword at a time for the three consecutive floats that land on
// the ray, so the search advances by the alignment the engine lays the struct out on.
constexpr int kFieldStride = 4;

void __cdecl InspectTrace(const std::uint32_t* args) {
    if (g_inOurTrace || g_gaveUp.load(std::memory_order_relaxed)) {
        return;
    }

    const AimMarker& marker = GetAimMarker();
    const float cx = marker.clean_x.load(std::memory_order_relaxed);
    const float cy = marker.clean_y.load(std::memory_order_relaxed);
    const float cz = marker.clean_z.load(std::memory_order_relaxed);
    if (cx == 0.0f && cy == 0.0f && cz == 0.0f) {
        return;
    }

    const auto* start = reinterpret_cast<const float*>(args[kArgStart]);
    const auto* end = reinterpret_cast<const float*>(args[kArgEnd]);
    if (!start || !end) {
        return;
    }
    if (g_argsUnproven.load(std::memory_order_relaxed)) {
        if (g_unprovenChecks.fetch_add(1, std::memory_order_relaxed) >= kMaxUnprovenChecks) {
            // Every thread already past the g_gaveUp test above reaches this, so the
            // exchange - not the test - is what makes the line appear once.
            if (!g_gaveUp.exchange(true, std::memory_order_relaxed)) {
                Log::Line("WARN: no line check was ever seen starting at the camera, so "
                          "the aim distance is unavailable and the reticle is projected "
                          "as a direction. A positional lean will slide it off what it "
                          "marks.");
            }
            return;
        }
        if (!Readable(start, sizeof(UE3Vector)) || !Readable(end, sizeof(UE3Vector))) {
            return;
        }
    }
    if (std::fabs(start[0] - cx) >= kStartMatchUnits ||
        std::fabs(start[1] - cy) >= kStartMatchUnits ||
        std::fabs(start[2] - cz) >= kStartMatchUnits) {
        return;
    }

    const float dx = end[0] - start[0], dy = end[1] - start[1], dz = end[2] - start[2];
    const float range = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(range > kMinCameraRayUnits)) {
        return;
    }

    g_matches.fetch_add(1, std::memory_order_relaxed);
    g_argsUnproven.store(false, std::memory_order_relaxed);

    TraceParams captured;
    captured.sourceActor = reinterpret_cast<void*>(args[kArgSourceActor]);
    captured.flags = args[kArgTraceFlags];
    captured.lastArg = args[kArgLast];
    captured.range = range;
    {
        std::lock_guard<std::mutex> lock(g_paramsMutex);
        g_params = captured;
    }
    // Latched separately from g_haveParams, which the gameplay gate clears on every
    // pause, level load and menu. Reusing that handshake for the log meant this line
    // reappeared on every resume, and the whole logging design is edges-only.
    static std::atomic<bool> s_logged{false};
    g_haveParams.store(true, std::memory_order_release);
    if (!s_logged.exchange(true, std::memory_order_relaxed)) {
        Log::Line("Aim trace parameters captured from the game: range=%.0f flags=0x%08X "
                  "source=0x%p", captured.range, captured.flags, captured.sourceActor);
    }
}

__declspec(naked) void HkSingleLineCheck() {
    __asm {
        pushad
        pushfd
        // pushad is 32 bytes and pushfd 4, and the return address sits above those, so
        // the caller's first argument is at esp+0x28.
        lea eax, [esp + 0x28]
        push eax
        call InspectTrace
        add esp, 4
        popfd
        popad
        jmp [g_original]
    }
}

// One snapshot of the parameters captured from the game's own crosshair check. The
// source actor, the flags and the range have to come from the SAME capture: pairing one
// capture's actor with another's range casts a ray that ignores the wrong object. The
// actor is the player's own pawn, and the gameplay gate drops the whole capture the
// moment the controller stops possessing one.
bool SnapshotParams(TraceParams* out) {
    if (!g_haveParams.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_paramsMutex);
    *out = g_params;
    return true;
}

// Casts `range` engine units from `origin` along the unit vector `dir` with the game's
// own crosshair parameters, and returns the distance along `dir` to whatever stopped it.
//
// False when the ray reached the end of its range without hitting anything, and false
// when the hit location could not be read out of the result: FCheckResult's layout is
// not known ahead of time, so Location is found once by looking for the three
// consecutive floats that land on the ray just cast, and reused from then on.
bool CastAlong(const TraceParams& params, const float origin[3], const float dir[3],
               float range, float* outUnits) {
    if (!g_worldPtr) {
        return false;
    }
    void* world = *g_worldPtr;
    if (!world) {
        return false;
    }

    const UE3Vector start{ origin[0], origin[1], origin[2] };
    const UE3Vector dir3{ dir[0], dir[1], dir[2] };
    const UE3Vector end{ start.X + dir3.X * range,
                         start.Y + dir3.Y * range,
                         start.Z + dir3.Z * range };
    const UE3Vector extent{ 0.0f, 0.0f, 0.0f };

    alignas(16) std::uint8_t hit[kCheckResultBytes];
    std::memset(hit, 0, sizeof(hit));
    const int clear = [&] {
        OurTraceScope scope;
        return g_lineCheck(world, nullptr, hit, params.sourceActor, &end, &start,
                           params.flags, &extent, params.lastArg);
    }();
    if (clear != 0) {
        return false;
    }

    int offset = g_locationOffset.load(std::memory_order_relaxed);
    if (offset < 0) {
        for (int off = 0; off + static_cast<int>(sizeof(UE3Vector)) <= kCheckResultBytes;
             off += kFieldStride) {
            const auto* p = reinterpret_cast<const float*>(hit + off);
            if (OnRay(p, start, dir3, range)) {
                offset = off;
                g_locationOffset.store(off, std::memory_order_relaxed);
                Log::Line("FCheckResult::Location at +0x%02X", off);
                break;
            }
        }
        if (offset < 0) {
            return false;
        }
    }

    const auto* loc = reinterpret_cast<const float*>(hit + offset);
    const float dx = loc[0] - start.X, dy = loc[1] - start.Y, dz = loc[2] - start.Z;
    const float distance = dx * dir3.X + dy * dir3.Y + dz * dir3.Z;
    if (!std::isfinite(distance) || distance < 0.0f || distance > range) {
        return false;
    }
    *outUnits = distance;
    return true;
}

}  // namespace

bool InstallAimTrace(std::uintptr_t singleLineCheckAddr, std::uintptr_t worldPtrAddr) {
    g_lineCheck = reinterpret_cast<SingleLineCheck_t>(singleLineCheckAddr);
    g_worldPtr = reinterpret_cast<void**>(worldPtrAddr);

    g_target = reinterpret_cast<void*>(singleLineCheckAddr);
    const MH_STATUS st = CreateAndEnableHook(
        g_target, reinterpret_cast<void*>(&HkSingleLineCheck), &g_original);
    if (st != MH_OK) {
        Log::Line("WARN: watching UWorld::SingleLineCheck @ 0x%p failed: %d. The reticle "
                  "will be projected as a direction, so a positional lean will slide it "
                  "off what it marks.", g_target, st);
        // MH_CreateHook may have succeeded and only the enable failed, in which case the
        // entry and its trampoline are allocated. Nulling g_target alone strands them.
        MH_RemoveHook(g_target);
        g_target = nullptr;
        return false;
    }
    Log::Line("Aim trace watching UWorld::SingleLineCheck @ 0x%p", g_target);
    return true;
}

void InvalidateAimTrace() {
    g_haveParams.store(false, std::memory_order_release);
}

void RemoveAimTrace() {
    if (g_target) {
        MH_DisableHook(g_target);
        MH_RemoveHook(g_target);
        g_target = nullptr;
    }
    g_haveParams.store(false, std::memory_order_release);
    g_argsUnproven.store(true, std::memory_order_relaxed);
}

bool AimDistance(const float eye[3], const float fwd[3], float* outUnits) {
    TraceParams params;
    if (!SnapshotParams(&params)) {
        return false;
    }

    // Cast on the frame that consumes it, with the game's own flags and range, so the
    // reticle lands on the surface the bullet stops on rather than a smoothed memory of
    // an older one. A definite no-hit is a target at infinity, and the caller projects
    // the aim direction - which is exactly right for a shot into the sky.
    float distance = 0.0f;
    if (!CastAlong(params, eye, fwd, params.range, &distance)) {
        return false;
    }
    // Under a centimetre out is the camera resting against a surface, which is not a
    // depth the reticle can be projected at.
    if (!(distance > 1.0f)) {
        return false;
    }
    g_hits.fetch_add(1, std::memory_order_relaxed);
    *outUnits = distance;
    return true;
}

bool TraceClearance(const float start[3], const float dir[3], float length,
                    float* outUnits) {
    if (!(length > 0.0f)) {
        return false;
    }
    TraceParams params;
    if (!SnapshotParams(&params)) {
        return false;
    }
    return CastAlong(params, start, dir, length, outUnits);
}

unsigned long AimTraceMatchCount() {
    return g_matches.load(std::memory_order_relaxed);
}

unsigned long AimTraceHitCount() {
    return g_hits.load(std::memory_order_relaxed);
}

}  // namespace SpecOpsTheLineHeadTracking
