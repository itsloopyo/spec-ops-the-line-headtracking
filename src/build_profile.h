#pragma once

#include "cameraunlock/memory/pe_fingerprint.h"

#include <cstdint>

namespace SpecOpsTheLineHeadTracking {

// One shipped Spec Ops: The Line build: its PE fingerprint and the RVAs the camera hook
// pins to. Append-only registry: a patch that moves RVAs gets a NEW profile added to
// the top of kKnownProfiles, never an in-place edit, so users on older builds keep
// matching their original profile by fingerprint.
struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint fingerprint;

    // RVA of APlayerController::GetPlayerViewPoint(FVector&, FRotator&), the
    // viewpoint accessor the scene-view builder and the aim path both call.
    std::uintptr_t rvaGetPlayerViewPoint;

    // RVA of the instruction the scene-view builder returns to after its call to
    // GetPlayerViewPoint. The hook adds the head pose only for that one caller,
    // so rendering is head-tracked while aim keeps the clean viewpoint.
    std::uintptr_t rvaSceneViewCallSite;

    // RVA of APlayerController::GetFOVAngle(), whose result the scene-view builder
    // feeds straight into its projection matrix. The reticle overlay needs the same
    // number to place the aim marker where the frame was actually projected.
    std::uintptr_t rvaGetFovAngle;

    // RVA of the instruction the scene-view builder returns to after its call to
    // GetFOVAngle. Two other callers share the accessor, so the field of view is both
    // published and scaled for this one only: the others feed game logic, which keeps
    // the field of view the game chose.
    std::uintptr_t rvaFovCallSite;

    // RVA of UYCrosshair::Draw(AYHUD*, FLOAT), which every crosshair class goes
    // through on its way to its own DrawCrosshair override. The crosshair hook brackets
    // it to move the drawn crosshair onto the aim point.
    std::uintptr_t rvaCrosshairDraw;

    // RVA of UWorld::SingleLineCheck, the world line check AActor::FastTrace runs. The
    // aim trace watches it for the check that starts at the camera, which is the one
    // that says how far away what the player is aiming at is.
    std::uintptr_t rvaSingleLineCheck;

    // RVA of the GWorld pointer the line check is called on.
    std::uintptr_t rvaWorldPtr;
};

// Most-recent build first (diagnostic primary).
extern const BuildProfile kKnownProfiles[];
extern const int kKnownProfileCount;

// Returns the profile matching the running EXE, or nullptr when no profile
// matches (mod stays dormant - no hooks installed).
const BuildProfile* MatchRunningProfile();

}  // namespace SpecOpsTheLineHeadTracking
