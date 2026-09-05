#include "camera_hook.h"

#include "aim_marker.h"
#include "aim_trace.h"
#include "game_state.h"
#include "logging.h"
#include "ue3_rotation.h"
#include "ue3_types.h"

#include "minhook_util.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cmath>

namespace SpecOpsTheLineHeadTracking {

namespace {

// APlayerController::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation),
// __thiscall with both out-pointers on the stack, modelled as __fastcall with a
// dummy edx so MinHook can detour it.
using GetPlayerViewPoint_t = void(__fastcall*)(void* thisptr, void* edx,
                                               UE3Vector* outLoc, UE3Rotator* outRot);

// APlayerController::GetFOVAngle(), the accessor the scene-view builder calls two
// instructions after the viewpoint one, whose result it passes straight into
// FPerspectiveMatrix as FOV * 0.5 * PI/180. Takes no arguments and returns degrees, so
// __fastcall with a dummy edx models the __thiscall exactly.
using GetFovAngle_t = float(__fastcall*)(void* thisptr, void* edx);

GetPlayerViewPoint_t g_original = nullptr;
GetFovAngle_t    g_originalFov = nullptr;
void*            g_target = nullptr;
void*            g_fovTarget = nullptr;
void*            g_sceneViewCallSite = nullptr;
void*            g_fovCallSite = nullptr;
// Read on the game thread inside the detour and cleared on unload. Atomic, and loaded
// once into a local for the whole detour: a plain pointer tested and then dereferenced
// lets the clear land between the two.
std::atomic<TrackingRuntime*> g_tracking{nullptr};
float            g_positionScale = 100.0f;
float            g_fovScale = 1.0f;
// Whether a lean is stopped at the level's geometry, and how far short of a surface the
// leaned view stops, in engine units (centimetres).
bool             g_collisionEnabled = defaults::kCollisionEnabled;
float            g_collisionPadding = defaults::kCollisionPadding;
// Incremented in the detours on the game thread and read on the heartbeat thread. Plain
// longs here are a data race whose practical cost is a wrong diagnosis rather than a
// wrong number: nothing stops the heartbeat's read being hoisted out of its loop, which
// latches "Camera detour: not firing" for a session in which it fires every frame.
std::atomic<unsigned long> g_totalCalls{0};
std::atomic<unsigned long> g_renderCalls{0};
std::atomic<unsigned long> g_fovRenderCalls{0};

// APlayerController::PlayerCamera, and the two fields of it the scene-view builder
// reads two instructions after the field of view: the constrain-aspect flag (bit 1 of
// the byte at +0x208) and the ratio it constrains to. With the flag set the builder
// hands FPerspectiveMatrix that ratio instead of the viewport's, so the frame is
// letterboxed inside the window and both the vertical field of view and the rectangle
// the reticle's coordinates span come from it. Measured live on this build: the flag is
// set throughout gameplay and the ratio is 1.7778, which is why the reticle looked
// right at 16:9 and would have drifted vertically at anything else.
constexpr std::size_t kPlayerCameraOffset      = 0x38C;
constexpr std::size_t kCameraAspectFlagsOffset = 0x208;
constexpr std::size_t kCameraConstrainedAspect = 0x210;
constexpr std::uint8_t kConstrainAspectBit     = 0x02;

// The aspect the scene view's projection is about to be built with, or 0 when the
// builder takes the viewport's own ratio and no letterboxing happens.
float ProjectionAspect(void* playerController) {
    void* camera = *reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(playerController) + kPlayerCameraOffset);
    if (!camera) {
        return 0.0f;
    }
    const std::uint8_t* base = static_cast<const std::uint8_t*>(camera);
    if ((*(base + kCameraAspectFlagsOffset) & kConstrainAspectBit) == 0) {
        return 0.0f;
    }
    return *reinterpret_cast<const float*>(base + kCameraConstrainedAspect);
}

// The positional lean applied to the render eye this frame: along the clean camera's
// right, up and forward axes, and as the world vector those add up to. Both are needed -
// the components for the diagnostic line, the world vector for the reticle, which has to
// know where the render eye moved to rather than how it got there.
struct Lean {
    float ruf[3] = { 0.0f, 0.0f, 0.0f };
    float world[3] = { 0.0f, 0.0f, 0.0f };

    bool IsZero() const {
        return world[0] == 0.0f && world[1] == 0.0f && world[2] == 0.0f;
    }
};

// The reticle marks a POINT, not a direction.
//
// With the eye where the game put it the two project to the same place, which is why a
// direction was enough until positional tracking landed. A lean breaks it: the frame is
// drawn from an eye up to a few tens of centimetres to one side of the one the shot
// leaves from, so the fixed impact point stops being straight ahead and a
// direction-based reticle slides off the thing it is marking - by roughly lean divided
// by distance, worse the closer the target.
//
// So the vector projected is from the RENDER eye to the impact point:
//
//     aim = (cleanEye + cleanForward * distance) - (cleanEye + lean)
//         = cleanForward * distance - lean
//
// with distance from the game's own crosshair ray (aim_trace.h). Without a distance the
// target is at infinity and the aim direction is the right answer, which is also exactly
// what this reduces to as distance grows.
//
// It must NOT be approximated with a fixed anchor distance. That makes the reticle exact
// at the chosen range and wrong either side, with the error CHANGING SIDES as the player
// crosses it, which reads as a broken reticle and sends the next person hunting a sign
// fault that is not there.
//
// The two bases come from the same RotatorToMatrix the injection used, so the reticle
// cannot drift out of agreement with the camera on a combined pose the way a per-axis
// Euler formula does.
void PublishAimMarker(const UE3Vector& cleanLoc, const UE3Rotator& clean,
                      const UE3Rotator& tracked, const Lean& lean) {
    const Mat3 cleanBasis = RotatorToMatrix(clean);
    const Mat3 trackedBasis = RotatorToMatrix(tracked);

    // The clean camera's forward is the direction the shot leaves along.
    float f[3];
    ForwardAxis(cleanBasis, f);
    const float eye[3] = { cleanLoc.X, cleanLoc.Y, cleanLoc.Z };

    float aim[3] = { f[0], f[1], f[2] };
    float distance = 0.0f;
    // With the render eye where the shot leaves from, the point and the direction project
    // to the same place, so the distance cannot change the answer and the ray is not cast
    // at all. Only a lean makes it matter.
    if (!lean.IsZero() && AimDistance(eye, f, &distance)) {
        aim[0] = f[0] * distance - lean.world[0];
        aim[1] = f[1] * distance - lean.world[1];
        aim[2] = f[2] * distance - lean.world[2];
    }

    // Normalised before publishing, so `forward` is a cosine in both modes. Unnormalised
    // it is the hit distance in centimetres whenever a lean folded one in, and
    // ProjectAim's kMinForward guard - which reads as an 84 degree cut-off - then admits
    // aim points within a hundredth of a degree of the view plane and hands the crosshair
    // an offset of millions of pixels. The projection itself is a pair of ratios, so
    // scaling the vector does not move the reticle by one pixel.
    const float len = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    AimMarker& marker = GetAimMarker();
    if (!std::isfinite(len) || len <= 0.0f) {
        // The render eye is exactly on the impact point, so no direction points at it.
        marker.active.store(false, std::memory_order_release);
        return;
    }
    aim[0] /= len;
    aim[1] /= len;
    aim[2] /= len;

    float forward = 0.0f, right = 0.0f, up = 0.0f;
    ResolveInBasis(trackedBasis, aim, &forward, &right, &up);

    marker.forward.store(forward, std::memory_order_relaxed);
    marker.right.store(right, std::memory_order_relaxed);
    marker.up.store(up, std::memory_order_relaxed);
    marker.distance.store(distance, std::memory_order_relaxed);
    marker.lean_right.store(lean.ruf[0], std::memory_order_relaxed);
    marker.lean_up.store(lean.ruf[1], std::memory_order_relaxed);
    marker.lean_forward.store(lean.ruf[2], std::memory_order_relaxed);
    // Release, paired with the acquire in ProjectAim. Relaxed stores to distinct atomics
    // may be reordered by the compiler, so `active` could otherwise be observed true
    // alongside the previous frame's - or, on the first activation, the initial -
    // components.
    marker.active.store(true, std::memory_order_release);
}

// The pose is dropped rather than written into the engine, and the reason is named once:
// a view that simply stops moving is otherwise indistinguishable from a dead tracker.
void ReportNonFiniteOnce(const FrameSample& s) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("WARN: the tracker pose is not a finite number "
              "(rot=(%.2f,%.2f,%.2f) pos=(%.3f,%.3f,%.3f)); nothing is being injected. "
              "The pose is used as sent, so this is coming from the tracker.",
              s.yaw, s.pitch, s.roll, s.pos_x, s.pos_y, s.pos_z);
}

// A shortening smaller than this is the probe finding the far end of its own range, not
// a wall the player leaned into. The clamp still happens; the line does not.
constexpr float kReportableClampUnits = 1.0f;

// One line, once. Proof the clamp is live, without a line a frame for as long as the
// player stands in cover.
void ReportFirstClampOnce(float requested, float allowed) {
    static bool reported = false;
    if (reported || requested - allowed <= kReportableClampUnits) {
        return;
    }
    reported = true;
    Log::Line("Camera collision: a %.1f cm lean was shortened to %.1f cm by the geometry "
              "in front of it. The view stops %.1f cm short of a surface instead of "
              "passing through it.",
              requested, allowed < 0.0f ? 0.0f : allowed, g_collisionPadding);
}

// The head pose moves the render eye AFTER the game has placed and collided its own
// camera, and nothing in the engine knows the eye has moved. So a lean towards a wall
// walks it straight through the surface and the player sees the level from inside the
// geometry - the game's own camera collision cannot help, because it ran against the
// un-leaned position.
//
// The lean is therefore cast as a ray from the clean camera and shortened to whatever it
// reaches: it keeps its direction and loses only its length. The ray is the game's own
// crosshair one - same trace flags, same actor to ignore - so it stops on what the level
// is built from rather than on a volume the player is standing in, and it does not run
// at all until the game has cast that ray once itself.
//
// The reticle is projected with the SHORTENED lean, because PublishAimMarker reads the
// same struct after this returns. The crosshair therefore keeps marking the point the
// shot lands on as seen from the eye the frame was actually drawn from.
void ClampLeanToGeometry(const UE3Vector& cleanLoc, Lean& lean) {
    if (!g_collisionEnabled) {
        return;
    }
    const float len = std::sqrt(lean.world[0] * lean.world[0] +
                                lean.world[1] * lean.world[1] +
                                lean.world[2] * lean.world[2]);
    if (!(len > 0.0f)) {
        return;
    }
    const float dir[3] = { lean.world[0] / len, lean.world[1] / len,
                           lean.world[2] / len };
    const float eye[3] = { cleanLoc.X, cleanLoc.Y, cleanLoc.Z };

    // Probed one padding beyond the lean, so a surface just past where the eye would have
    // stopped anyway still leaves the lean its full length.
    float hit = 0.0f;
    if (!TraceClearance(eye, dir, len + g_collisionPadding, &hit)) {
        return;
    }

    const float allowed = hit - g_collisionPadding;
    // Scaling the world vector by the same factor as its components is exact: the three
    // axes it was built from are orthonormal.
    const float scale = allowed > 0.0f ? allowed / len : 0.0f;
    if (scale >= 1.0f) {
        return;
    }
    ReportFirstClampOnce(len, allowed);
    for (int i = 0; i < 3; ++i) {
        lean.world[i] *= scale;
        lean.ruf[i] *= scale;
    }
}

Lean ApplyHeadPose(TrackingRuntime& tracking, UE3Vector* outLoc, UE3Rotator* outRot,
                   const FrameSample& s) {
    Lean lean;

    // The pose comes off the network and everything below writes it into the engine's
    // own out-parameters, so this is the boundary worth validating at: a NaN in the
    // rotator or in the camera's world position renders a black frame, and the
    // camera-local branch would carry it into lround, which is undefined for one.
    if (!std::isfinite(s.yaw) || !std::isfinite(s.pitch) || !std::isfinite(s.roll) ||
        !std::isfinite(s.pos_x) || !std::isfinite(s.pos_y) || !std::isfinite(s.pos_z)) {
        ReportNonFiniteOnce(s);
        return lean;
    }

    // The tracker declares no convention of its own, so the mirrored axes are
    // flipped here, once, where the tracker meets the engine. Yaw matches UE3
    // directly (verified in game); roll is mirrored, as are x and z.
    const float headYaw   =  s.yaw;
    const float headPitch =  s.pitch;
    const float headRoll  = -s.roll;

    // 6DOF position goes in the clean orientation basis, before head rotation is
    // added, so a lean follows body facing rather than the head-rotated view.
    // UE3 is left-handed with X forward, Y right, Z up.
    if (s.has_position) {
        // Horizon-locked: forward is FLAT, so the three axes are orthogonal and a lean
        // moves the eye by the amount asked for along the axis asked for. Using the
        // pitched forward instead put part of a forward lean into world Z, where the
        // vertical limits - already applied, in tracker space - could not see it: a
        // 0.40m lean with the camera pitched 40 degrees down also dropped the eye 0.26m
        // with nothing bounding that.
        const float yawRad = UnitsToRad(outRot->Yaw);
        const float cy = std::cos(yawRad), sy = std::sin(yawRad);

        const float fwd[3]   = { cy,   sy,   0.0f };
        const float right[3] = { -sy,  cy,   0.0f };
        const float up[3]    = { 0.0f, 0.0f, 1.0f };

        const float oR = -s.pos_x * g_positionScale;
        const float oU =  s.pos_y * g_positionScale;
        const float oF = -s.pos_z * g_positionScale;
        lean.ruf[0] = oR;
        lean.ruf[1] = oU;
        lean.ruf[2] = oF;

        // Kept in world units as well as in the clean basis: the reticle projection
        // needs the vector the render eye actually moved by, not its components.
        lean.world[0] = right[0] * oR + up[0] * oU + fwd[0] * oF;
        lean.world[1] = right[1] * oR + up[1] * oU + fwd[1] * oF;
        lean.world[2] = right[2] * oR + up[2] * oU + fwd[2] * oF;
        // Before the offset is written, and before the reticle is projected with it.
        ClampLeanToGeometry(*outLoc, lean);
        outLoc->X += lean.world[0];
        outLoc->Y += lean.world[1];
        outLoc->Z += lean.world[2];
    }

    if (!s.has_rotation) {
        return lean;
    }

    if (tracking.IsWorldSpaceYaw()) {
        // Horizon-locked yaw (default): FRotator yaw is the outermost rotation
        // about world Z, so per-axis addition keeps head yaw on the world
        // up-axis no matter how the camera is pitched. Pitch and roll stay
        // camera-relative.
        // Each axis is folded onto its half-turn BEFORE the add. The engine's rotator
        // fields are plain int32 and nothing bounds what the game put there, so adding
        // up to a revolution to a raw one is signed overflow, which is undefined. After
        // the fold both operands are inside +/-32768 and the sum cannot leave int32.
        outRot->Yaw   = WrapSigned(outRot->Yaw) + DegToUnits(headYaw);
        outRot->Roll  = WrapSigned(outRot->Roll) + DegToUnits(headRoll);
        // Pitch is stopped one unit short of vertical as well: a head pitch stacked on an
        // already-steep game camera would otherwise pass straight up and invert the
        // world, which the player cannot undo by looking back down.
        outRot->Pitch = ClampPitch(WrapSigned(outRot->Pitch) + DegToUnits(headPitch));
    } else {
        // Camera-local yaw: compose the head rotation in the camera frame
        // (M_head * M_clean, row-vector convention) so yaw follows the tilted
        // up-axis at extreme pitches. Coincides with the horizon-locked branch
        // when the clean camera is level.
        const Mat3 clean = RotatorToMatrix(*outRot);
        const Mat3 head = RotatorToMatrix(headPitch * kDegToRad, headYaw * kDegToRad,
                                          headRoll * kDegToRad);
        MatrixToRotator(MatMul(head, clean), outRot);
        // atan2 already bounds this branch at exactly vertical rather than past it, but
        // vertical is the singularity itself: the composition flips roll there. Stopped
        // one unit short, on the same terms as the branch above.
        outRot->Pitch = ClampPitch(outRot->Pitch);
    }
    return lean;
}

// Edge-triggered: "tracking did nothing" and "the mod thinks you are in a menu" are
// otherwise the same report, and a line per frame would bury both.
void ReportGameplayState(GameplayState state) {
    static GameplayState s_last = GameplayState::Playing;
    static bool s_reported = false;
    if (state == s_last && s_reported) {
        return;
    }
    s_last = state;
    s_reported = true;
    Log::Line("Gameplay: %s", Describe(state));
}

// One line, once, proving the pose reached the render view. A "no head tracking" report
// is otherwise ambiguous between a dead hook and a dead tracker, and the heartbeat can
// only tell those apart one level up.
void ReportFirstPoseOnce(const FrameSample& s) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("First tracked frame: rot=(%.2f,%.2f,%.2f) deg pos=(%.3f,%.3f,%.3f) m",
              s.yaw, s.pitch, s.roll, s.pos_x, s.pos_y, s.pos_z);
}

void __fastcall Detour(void* thisptr, void* edx, UE3Vector* outLoc, UE3Rotator* outRot) {
    g_original(thisptr, edx, outLoc, outRot);
    g_totalCalls.fetch_add(1, std::memory_order_relaxed);

    // GetPlayerViewPoint has 21 call sites, and the aim, trace and interaction
    // ones must keep seeing the clean camera - that is the whole decoupling. Only
    // the scene-view caller gets the head pose, identified by the address it
    // returns to. Everything else passes through untouched.
    if (_ReturnAddress() != g_sceneViewCallSite) {
        return;
    }
    g_renderCalls.fetch_add(1, std::memory_order_relaxed);

    TrackingRuntime* tracking = g_tracking.load(std::memory_order_acquire);
    if (!tracking || !outLoc || !outRot) {
        return;
    }

    // The main menu and the pause screen both build the scene view at the full frame
    // rate, so without this the head pose would keep swinging the camera while the
    // player reads a menu.
    const GameplayState state = GetGameplayState(thisptr);
    ReportGameplayState(state);
    if (state != GameplayState::Playing) {
        GetAimMarker().active.store(false, std::memory_order_relaxed);
        // The captured trace parameters name the player's own pawn, which a level load,
        // a death or a chapter change destroys. Dropping them here means the next cast
        // waits for the game to run its own crosshair check again rather than handing
        // the engine a pointer to a freed actor.
        InvalidateAimTrace();
        return;
    }

    const FrameSample s = tracking->SampleFrame();
    if (!s.has_rotation && !s.has_position) {
        // Nothing injected this frame, so the rendered view IS the aim and the game's
        // own centred crosshair marks the shot. Anything the overlay drew would be a
        // second crosshair claiming otherwise.
        GetAimMarker().active.store(false, std::memory_order_relaxed);
        return;
    }
    ReportFirstPoseOnce(s);

    const UE3Rotator clean = *outRot;
    const UE3Vector cleanLoc = *outLoc;
    AimMarker& marker = GetAimMarker();
    marker.clean_x.store(cleanLoc.X, std::memory_order_relaxed);
    marker.clean_y.store(cleanLoc.Y, std::memory_order_relaxed);
    marker.clean_z.store(cleanLoc.Z, std::memory_order_relaxed);
    const Lean lean = ApplyHeadPose(*tracking, outLoc, outRot, s);
    PublishAimMarker(cleanLoc, clean, *outRot, lean);
}

// One line, once, naming every term of the projection the frame is about to be drawn
// with. The angle alone does not place the reticle: the aspect decides the vertical
// half-angle and, when the builder constrains it, the rectangle inside the window that
// the reticle's coordinates span.
void ReportProjectionOnce(float gameFov, float renderedFov, float aspect) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    if (aspect > 0.0f) {
        Log::Line("Scene view: %.1f deg horizontal (game %.1f, scale %.3f), projected at "
                  "a constrained %.4f aspect - the frame is letterboxed inside the window "
                  "and the reticle follows that rectangle",
                  renderedFov, gameFov, g_fovScale, aspect);
    } else {
        Log::Line("Scene view: %.1f deg horizontal (game %.1f, scale %.3f), projected at "
                  "the viewport's own aspect", renderedFov, gameFov, g_fovScale);
    }
}

// The scene-view builder calls this two instructions after the viewpoint accessor, so
// the field of view the overlay reads is the one the frame it is drawn over was
// projected with, not the previous frame's.
//
// Two other callers share the accessor and both are game logic, so the scale and the
// publish are gated on the return address the same way the viewpoint injection is.
// Widening what the game decides with would change weapon behaviour; widening the
// picture does not, and leaving the publish ungated would hand the overlay whichever
// of the three callers ran last.
float __fastcall FovDetour(void* thisptr, void* edx) {
    const float fov = g_originalFov(thisptr, edx);
    if (_ReturnAddress() != g_fovCallSite) {
        return fov;
    }
    g_fovRenderCalls.fetch_add(1, std::memory_order_relaxed);

    const float rendered = fov * g_fovScale;
    const float aspect = ProjectionAspect(thisptr);
    ReportProjectionOnce(fov, rendered, aspect);
    AimMarker& marker = GetAimMarker();
    marker.fov_deg.store(rendered, std::memory_order_relaxed);
    marker.projection_aspect.store(aspect, std::memory_order_relaxed);
    return rendered;
}

bool InstallViewPointHook(std::uintptr_t address) {
    g_target = reinterpret_cast<void*>(address);
    const MH_STATUS st = CreateAndEnableHook(g_target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking APlayerController::GetPlayerViewPoint @ 0x%p failed: %d",
                  g_target, st);
        // Create may have succeeded with only the enable failing, leaving the entry and
        // its trampoline allocated; nulling g_target alone strands them.
        MH_RemoveHook(g_target);
        g_target = nullptr;
        g_original = nullptr;
        return false;
    }
    return true;
}

// The marker needs the field of view the frame was projected with, and the only number
// that is guaranteed to be is the one the scene-view builder itself uses. A failure here
// costs the marker, not the tracking, so it warns rather than aborting the install.
void InstallFovHook(std::uintptr_t address) {
    g_fovTarget = reinterpret_cast<void*>(address);
    const MH_STATUS st = CreateAndEnableHook(g_fovTarget, reinterpret_cast<void*>(&FovDetour),
                                             reinterpret_cast<void**>(&g_originalFov));
    if (st != MH_OK) {
        Log::Line("WARN: hooking APlayerController::GetFOVAngle @ 0x%p failed: %d. The "
                  "aim marker will be projected with a default field of view and will "
                  "not sit on the aim point.", g_fovTarget, st);
        MH_RemoveHook(g_fovTarget);
        g_fovTarget = nullptr;
        g_originalFov = nullptr;
    }
}

}  // namespace

bool InstallCameraHook(const CameraHookTargets& targets, TrackingRuntime& tracking,
                       const Config& cfg) {
    g_tracking.store(&tracking, std::memory_order_release);
    g_positionScale = cfg.position_scale;
    g_fovScale = cfg.fov_scale;
    g_collisionEnabled = cfg.collision_enabled;
    g_collisionPadding = cfg.collision_padding;
    g_sceneViewCallSite = reinterpret_cast<void*>(targets.sceneViewCallSite);
    g_fovCallSite = reinterpret_cast<void*>(targets.fovCallSite);

    // The reticle overlay arms earlier in startup, so MinHook is usually already up by
    // the time the camera hook goes in.
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        Log::Line("ERROR: MH_Initialize failed: %d", init);
        return false;
    }

    if (!InstallViewPointHook(targets.getPlayerViewPoint)) {
        return false;
    }
    InstallFovHook(targets.getFovAngle);

    Log::Line("Camera hook installed: APlayerController::GetPlayerViewPoint @ 0x%p, "
              "scene-view call site 0x%p, GetFOVAngle @ 0x%p from 0x%p, "
              "field of view scale %.3f, lean collision %s (%.1f cm clearance)",
              g_target, g_sceneViewCallSite, g_fovTarget, g_fovCallSite, g_fovScale,
              g_collisionEnabled ? "on" : "off", g_collisionPadding);
    return true;
}

void RemoveCameraHook() {
    GetAimMarker().active.store(false, std::memory_order_relaxed);
    if (g_target) {
        MH_DisableHook(g_target);
        MH_RemoveHook(g_target);
        g_target = nullptr;
    }
    if (g_fovTarget) {
        MH_DisableHook(g_fovTarget);
        MH_RemoveHook(g_fovTarget);
        g_fovTarget = nullptr;
    }
    g_tracking.store(nullptr, std::memory_order_release);
}

unsigned long CameraHookCallCount() {
    return g_renderCalls.load(std::memory_order_relaxed);
}

unsigned long CameraHookNonRenderCallCount() {
    // Render first, then total. The detour increments total before render, so total is
    // never behind render at any instant, and reading render first can only make it
    // smaller - the subtraction cannot wrap. The other order can, and an unsigned wrap
    // reads as a huge non-render count, which is exactly the condition the heartbeat
    // turns into a "the rendered view is not being tracked" warning.
    const unsigned long render = g_renderCalls.load(std::memory_order_relaxed);
    const unsigned long total = g_totalCalls.load(std::memory_order_relaxed);
    return total - render;
}

unsigned long FovHookRenderCallCount() {
    return g_fovRenderCalls.load(std::memory_order_relaxed);
}

}  // namespace SpecOpsTheLineHeadTracking
