#include "aim_marker.h"

namespace SpecOpsTheLineHeadTracking {

namespace {
// Namespace scope rather than a function-local static: every member is
// constant-initialised, so there is no one-time-init guard for the callers to test.
// This is read several times a frame from the scene-view hook, the crosshair draw and
// the D3D9 present hook, which are the three hottest paths the mod has.
AimMarker g_marker;
}  // namespace

AimMarker& GetAimMarker() {
    return g_marker;
}

}  // namespace SpecOpsTheLineHeadTracking
