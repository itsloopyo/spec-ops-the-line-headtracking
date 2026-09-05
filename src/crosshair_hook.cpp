#include "crosshair_hook.h"

#include "aim_marker.h"
#include "aim_projection.h"
#include "logging.h"

#include "minhook_util.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>

namespace SpecOpsTheLineHeadTracking {

namespace {

// UYCrosshair::Draw(AYHUD*, FLOAT): __thiscall with both parameters on the stack
// (ret 8), modelled as __fastcall with a dummy edx.
//
// Confirmed by the vtable family rather than by name: this address sits at slot +0x130
// of the nine .rdata vtables spaced 0x148 apart - the UYCrosshair subclasses - and
// forwards to slot +0x134, the subclass's DrawCrosshair, whose seven distinct overrides
// are all `ret 8`. Bracketing this one function therefore covers every crosshair the
// game selects, with no hook per weapon class.
using CrosshairDraw_t = void(__fastcall*)(void* self, void* edx, void* hud, float delta);

// The screen position every DrawCrosshair override draws from. Six of the seven
// overrides read these two floats (the other two are the empty NoCrosshair and a
// five-byte stub); AYHUD::DrawTileRotated reaches the same HUD's canvas at +0x444.
// Offsetting them across the call moves the drawn crosshair and nothing else: the values
// are restored before returning, so the ammo counter and the rest of the HUD are
// untouched, and a frame that draws no crosshair never reaches this code.
constexpr std::size_t kHudCrosshairX = 0x458;
constexpr std::size_t kHudCrosshairY = 0x45C;

// AYHUD's canvas, logged once beside the crosshair position so a report that the
// crosshair did not move can be checked against the object the overrides draw through.
constexpr std::size_t kHudCanvas = 0x444;

CrosshairDraw_t g_original = nullptr;
void*           g_target = nullptr;
// Written on the game thread, read on the heartbeat thread. Plain longs here are a data
// race, and the practical cost is not a torn count: the heartbeat's read can be hoisted
// out of its loop, latching "the crosshair never drew" for the session and pointing a
// report at a dead hook that is in fact firing.
std::atomic<unsigned long> g_calls{0};
std::atomic<unsigned long> g_moved{0};
// Frames where the aim point could not be placed on the frame, so no crosshair was
// drawn at all. Counted because "the crosshair vanished" needs a number behind it.
std::atomic<unsigned long> g_suppressed{0};

float* HudField(void* hud, std::size_t offset) {
    return reinterpret_cast<float*>(static_cast<std::uint8_t*>(hud) + offset);
}

// Puts the two crosshair floats back however the call leaves. A straight-line restore
// after a call into game code is lost on an unwind, and the loss compounds: the saved
// value is read from the field each frame, so a field left offset is offset again next
// frame, and again, with nothing in the HUD that rewrites it.
class CrosshairOffset {
public:
    CrosshairOffset(float* x, float* y, float dx, float dy)
        : m_x(x), m_y(y), m_savedX(*x), m_savedY(*y) {
        *m_x = m_savedX + dx;
        *m_y = m_savedY + dy;
    }
    ~CrosshairOffset() {
        *m_x = m_savedX;
        *m_y = m_savedY;
    }
    CrosshairOffset(const CrosshairOffset&) = delete;
    CrosshairOffset& operator=(const CrosshairOffset&) = delete;

private:
    float* m_x;
    float* m_y;
    float  m_savedX;
    float  m_savedY;
};

// Only the outermost Draw offsets. If any subclass override reaches slot +0x130 again
// for a sub-crosshair, the inner call would otherwise save the already-offset value and
// apply the offset a second time.
thread_local int g_drawDepth = 0;

class DrawDepth {
public:
    DrawDepth() : m_outermost(g_drawDepth++ == 0) {}
    ~DrawDepth() { --g_drawDepth; }
    bool IsOutermost() const { return m_outermost; }
    DrawDepth(const DrawDepth&) = delete;
    DrawDepth& operator=(const DrawDepth&) = delete;

private:
    bool m_outermost;
};

// The HUD draws in the pixels of the letterboxed band the world is rendered into, not
// the window's: measured at 1280x800, where the game constrains the view to 16:9, the
// crosshair rests at (640, 360) - the middle of the 1280x720 band - rather than at
// (640, 400). So the projection's normalised coordinates scale by half that band, which
// is what ViewRectFor returns. Reported once with the resting position, because that
// equality is the assumption the whole offset rests on.
void ReportOnce(const void* hud, float x, float y, float width, float height) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("Crosshair at (%.1f, %.1f) on a %.0fx%.0f image; HUD 0x%p canvas 0x%p",
              x, y, width, height, hud,
              *reinterpret_cast<void* const*>(
                  static_cast<const std::uint8_t*>(hud) + kHudCanvas));
}

void __fastcall DrawDetour(void* self, void* edx, void* hud, float delta) {
    g_calls.fetch_add(1, std::memory_order_relaxed);

    const DrawDepth depth;

    // The two crosshair floats and the canvas pointer are read and WRITTEN through this
    // pointer at offsets taken from a disassembly, so it is validated before it is used
    // rather than trusted. A null HUD would put a four-byte write at 0x458.
    if (!hud || !depth.IsOutermost()) {
        g_original(self, edx, hud, delta);
        return;
    }

    const AimMarker& marker = GetAimMarker();
    const float width = marker.render_width.load(std::memory_order_relaxed);
    const float height = marker.render_height.load(std::memory_order_relaxed);

    // No render size yet means the overlay has not drawn a frame, so there is nothing to
    // scale by. Not a fallback: with tracking inactive the crosshair is already on the
    // shot, which is exactly what ProjectAim reports.
    if (width <= 0.0f || height <= 0.0f) {
        g_original(self, edx, hud, delta);
        return;
    }

    float* x = HudField(hud, kHudCrosshairX);
    float* y = HudField(hud, kHudCrosshairY);
    // After the size guard, so the one line this ever writes names a real image rather
    // than the 0x0 of a frame the overlay had not reached yet - and the equality it
    // exists to record is between the resting position and that image's band centre.
    ReportOnce(hud, *x, *y, width, height);

    const ViewRect view = ViewRectFor(width, height);
    float ndcX = 0.0f, ndcY = 0.0f;
    const AimProjection projected = ProjectAim(view.width / view.height, &ndcX, &ndcY);

    // Tracking is not being injected, so the rendered view IS the aim and the game's own
    // centred crosshair already marks the shot.
    if (projected == AimProjection::Inactive) {
        g_original(self, edx, hud, delta);
        return;
    }

    // The aim point is off the frame, behind the camera, or could not be projected. A
    // crosshair drawn anyway claims the shot lands where it is drawn, and it does not:
    // at screen centre that is a lie the player aims by, and at the edge it is a lie
    // with a direction. Draw nothing instead - the overlay hides its own marker on
    // exactly these cases, and the two must not disagree about the same frame.
    const float offsetX = ndcX * 0.5f * view.width;
    const float offsetY = -ndcY * 0.5f * view.height;
    if (projected != AimProjection::Ok ||
        !std::isfinite(offsetX) || !std::isfinite(offsetY) ||
        std::fabs(ndcX) > 1.0f || std::fabs(ndcY) > 1.0f) {
        g_suppressed.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    {
        const CrosshairOffset offset(x, y, offsetX, offsetY);
        g_original(self, edx, hud, delta);
    }
    g_moved.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

bool InstallCrosshairHook(std::uintptr_t crosshairDrawAddr) {
    g_target = reinterpret_cast<void*>(crosshairDrawAddr);
    const MH_STATUS st = CreateAndEnableHook(g_target, reinterpret_cast<void*>(&DrawDetour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking UYCrosshair::Draw @ 0x%p failed: %d", g_target, st);
        // Create may have succeeded with only the enable failing, leaving the entry and
        // its trampoline allocated; nulling g_target alone strands them.
        MH_RemoveHook(g_target);
        g_target = nullptr;
        return false;
    }
    Log::Line("Crosshair hook installed: UYCrosshair::Draw @ 0x%p", g_target);
    return true;
}

void RemoveCrosshairHook() {
    if (g_target) {
        MH_DisableHook(g_target);
        MH_RemoveHook(g_target);
        g_target = nullptr;
    }
}

unsigned long CrosshairDrawCount() {
    return g_calls.load(std::memory_order_relaxed);
}

unsigned long CrosshairMovedCount() {
    return g_moved.load(std::memory_order_relaxed);
}

unsigned long CrosshairSuppressedCount() {
    return g_suppressed.load(std::memory_order_relaxed);
}

}  // namespace SpecOpsTheLineHeadTracking
