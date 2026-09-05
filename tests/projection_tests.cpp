#include "test_support.h"

#include "aim_marker.h"
#include "aim_projection.h"
#include "ue3_types.h"

#include <cmath>
#include <initializer_list>

using namespace SpecOpsTheLineHeadTracking;

namespace {

void SetMarker(float right, float up, float forward, float fovDeg, float aspect,
               bool active) {
    AimMarker& m = GetAimMarker();
    m.right.store(right, std::memory_order_relaxed);
    m.up.store(up, std::memory_order_relaxed);
    m.forward.store(forward, std::memory_order_relaxed);
    m.fov_deg.store(fovDeg, std::memory_order_relaxed);
    m.projection_aspect.store(aspect, std::memory_order_relaxed);
    m.active.store(active, std::memory_order_relaxed);
}

// The game constrains its scene view to a fixed aspect, so at any other window ratio the
// world is letterboxed and the reticle's coordinates span the band, not the window.
int ViewRectTests() {
    int failures = 0;

    // No constrained aspect published: the projection spans the whole image.
    SetMarker(0.0f, 0.0f, 1.0f, 72.0f, 0.0f, false);
    ViewRect r = ViewRectFor(1280.0f, 800.0f);
    CHECK_NEAR(r.x, 0.0, 1e-4);
    CHECK_NEAR(r.y, 0.0, 1e-4);
    CHECK_NEAR(r.width, 1280.0, 1e-4);
    CHECK_NEAR(r.height, 800.0, 1e-4);

    // 16:9 into a 16:10 window: a 1280x720 band 40 pixels down, as measured in game.
    SetMarker(0.0f, 0.0f, 1.0f, 72.0f, 16.0f / 9.0f, false);
    r = ViewRectFor(1280.0f, 800.0f);
    CHECK_NEAR(r.x, 0.0, 1e-3);
    CHECK_NEAR(r.y, 40.0, 1e-3);
    CHECK_NEAR(r.width, 1280.0, 1e-3);
    CHECK_NEAR(r.height, 720.0, 1e-3);

    // Matching ratios degenerate to the whole image.
    r = ViewRectFor(1920.0f, 1080.0f);
    CHECK_NEAR(r.x, 0.0, 1e-2);
    CHECK_NEAR(r.y, 0.0, 1e-2);
    CHECK_NEAR(r.width, 1920.0, 1e-2);
    CHECK_NEAR(r.height, 1080.0, 1e-2);

    // Wider window than the projection: pillarboxed, centred horizontally.
    SetMarker(0.0f, 0.0f, 1.0f, 72.0f, 4.0f / 3.0f, false);
    r = ViewRectFor(1600.0f, 900.0f);
    CHECK_NEAR(r.x, 200.0, 1e-3);
    CHECK_NEAR(r.y, 0.0, 1e-3);
    CHECK_NEAR(r.width, 1200.0, 1e-3);
    CHECK_NEAR(r.height, 900.0, 1e-3);

    // A non-finite aspect is not trusted with the frame's geometry.
    SetMarker(0.0f, 0.0f, 1.0f, 72.0f, std::nanf(""), false);
    r = ViewRectFor(1280.0f, 720.0f);
    CHECK_NEAR(r.width, 1280.0, 1e-4);
    CHECK_NEAR(r.height, 720.0, 1e-4);
    return failures;
}

int ProjectAimTests() {
    int failures = 0;
    float ndcX = -99.0f, ndcY = -99.0f;

    // Nothing injected this frame: the game's own centred crosshair is the marker.
    SetMarker(0.0f, 0.0f, 1.0f, 72.0f, 0.0f, false);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Inactive);

    // Aim dead ahead lands at screen centre.
    SetMarker(0.0f, 0.0f, 1.0f, 72.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Ok);
    CHECK_NEAR(ndcX, 0.0, 1e-5);
    CHECK_NEAR(ndcY, 0.0, 1e-5);

    // Exactly the horizontal half-angle lands on the right edge; the vertical
    // half-angle follows from the aspect, so the same tangent over 16:9 is 16/9 of a
    // half-height up.
    const float tanH = std::tan(72.0f * 0.5f * static_cast<float>(kPi) / 180.0f);
    SetMarker(tanH, 0.0f, 1.0f, 72.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Ok);
    CHECK_NEAR(ndcX, 1.0, 1e-4);
    CHECK_NEAR(ndcY, 0.0, 1e-5);

    SetMarker(0.0f, tanH, 1.0f, 72.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Ok);
    CHECK_NEAR(ndcY, 16.0 / 9.0, 1e-4);

    // Halving the field of view doubles the offset for the same aim.
    SetMarker(tanH * 0.5f, 0.0f, 1.0f, 72.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Ok);
    CHECK_NEAR(ndcX, 0.5, 1e-4);

    // The aim has swung out of the frame: hidden, not clamped, and guarded before the
    // divide diverges.
    SetMarker(1.0f, 0.0f, 0.05f, 72.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Behind);
    SetMarker(1.0f, 0.0f, -1.0f, 72.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Behind);

    // A field of view the frame cannot have been drawn at is refused rather than used.
    SetMarker(0.0f, 0.0f, 1.0f, 5.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::BadFov);
    SetMarker(0.0f, 0.0f, 1.0f, 200.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::BadFov);
    SetMarker(0.0f, 0.0f, 1.0f, std::nanf(""), 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::BadFov);

    SetMarker(0.0f, 0.0f, 1.0f, 72.0f, 0.0f, false);
    return failures;
}

// kMinForward reads as an 84 degree cut-off ONLY because the camera hook publishes a
// unit direction. It used to publish `forward * distance - lean`, whose forward
// component is the hit distance in centimetres, and the same 0.1 then admitted aim
// points a hundredth of a degree off the view plane - which the crosshair hook turned
// into an offset of millions of pixels written into the HUD. These lock the contract
// down from both ends: the guard is an angle, and a bounded angle is a bounded NDC.
int ForwardGuardIsAnAngleTests() {
    int failures = 0;
    float ndcX = 0.0f, ndcY = 0.0f;

    // Just inside the documented cut-off, as a unit direction.
    const float insideDeg = 80.0f;
    SetMarker(std::sin(insideDeg * kDegToRadians), 0.0f,
              std::cos(insideDeg * kDegToRadians), 72.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Ok);

    // Just outside it.
    const float outsideDeg = 86.0f;
    SetMarker(std::sin(outsideDeg * kDegToRadians), 0.0f,
              std::cos(outsideDeg * kDegToRadians), 72.0f, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Behind);

    // The same geometry scaled by a trace distance must give the SAME verdict. Before
    // normalisation this passed the guard and produced an NDC in the thousands.
    for (float scale : { 100.0f, 5000.0f }) {
        SetMarker(std::sin(outsideDeg * kDegToRadians) * scale, 0.0f,
                  std::cos(outsideDeg * kDegToRadians) * scale, 72.0f, 0.0f, true);
        CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Behind);
    }

    // Anything the guard admits projects to an NDC the crosshair hook can use: at the
    // 84 degree limit and the narrowest field of view the guard allows, the magnitude
    // stays within a small multiple of the frame rather than reaching the thousands.
    SetMarker(std::sin(84.0f * kDegToRadians), 0.0f, std::cos(84.0f * kDegToRadians),
              kMinFovDeg, 0.0f, true);
    CHECK(ProjectAim(16.0f / 9.0f, &ndcX, &ndcY) == AimProjection::Ok);
    CHECK(std::fabs(ndcX) < 60.0f);

    SetMarker(0.0f, 0.0f, 1.0f, 72.0f, 0.0f, false);
    return failures;
}

}  // namespace

int RunProjectionTests() {
    std::printf("Aim projection\n");
    int failures = ViewRectTests();
    failures += ProjectAimTests();
    failures += ForwardGuardIsAnAngleTests();
    return failures;
}
