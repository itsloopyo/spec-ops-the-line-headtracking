#pragma once

#include "config.h"
#include "tracking_runtime.h"

#include <cstdint>

namespace SpecOpsTheLineHeadTracking {

// The four addresses the camera hook pins, resolved from the matched build profile.
// Grouped rather than passed as four bare addresses because they are the same type and
// swapping two of them detours the wrong function, which crashes the game on the first
// frame with nothing in the log to say why.
struct CameraHookTargets {
    // APlayerController::GetPlayerViewPoint(FVector&, FRotator&), the viewpoint accessor
    // the scene-view builder and the aim path both call.
    std::uintptr_t getPlayerViewPoint;
    // The instruction the scene-view builder returns to after its call to the accessor
    // above. Only that one caller gets the head pose.
    std::uintptr_t sceneViewCallSite;
    // APlayerController::GetFOVAngle(), whose result the scene-view builder feeds
    // straight into its projection matrix.
    std::uintptr_t getFovAngle;
    // The instruction the scene-view builder returns to after its call to GetFOVAngle.
    std::uintptr_t fovCallSite;
};

// Detours APlayerController::GetPlayerViewPoint and adds the head pose to the
// viewpoint it returns, but only when the caller is the scene-view builder
// (identified by targets.sceneViewCallSite, the return address of that one call).
// Aim, traces and interaction call the same function and keep the clean
// viewpoint, which is what decouples where the player looks from where the
// weapon shoots. Returns false if the detour could not be installed.
//
// Also detours targets.getFovAngle, the accessor the scene-view builder takes its field
// of view from, and publishes both that and the clean aim direction to the AimMarker the
// reticle overlay draws. That accessor has two other callers, both game logic, so
// targets.fovCallSite selects the one call whose result is scaled by Config::fov_scale
// and published. The game keeps its own field of view everywhere it makes a decision
// with one.
bool InstallCameraHook(const CameraHookTargets& targets, TrackingRuntime& tracking,
                       const Config& cfg);

void RemoveCameraHook();

// Times the detour ran for the scene view, so the heartbeat reports whether the
// rendered view is actually being tracked.
unsigned long CameraHookCallCount();

// Times it ran for any other caller. Non-zero with a zero render count means the
// scene view is taking a different branch and no tracking will be visible.
unsigned long CameraHookNonRenderCallCount();

// Times GetFOVAngle ran for the scene-view caller. Zero while the viewpoint detour is
// firing means the pinned call site is wrong: the marker would then be projected with a
// field of view the frame was never drawn at, and any FieldOfView.Scale would do nothing.
unsigned long FovHookRenderCallCount();

}  // namespace SpecOpsTheLineHeadTracking
