#include "test_support.h"

#include "config_sanitize.h"

#include <limits>

using namespace SpecOpsTheLineHeadTracking;

namespace {

int SmoothingTests() {
    int failures = 0;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // Validation, not a floor: anything inside [0,1] reaches the processor untouched.
    CHECK_NEAR(SanitizeSmoothing(0.0f, 0.15f), 0.0, 1e-6);
    CHECK_NEAR(SanitizeSmoothing(0.42f, 0.15f), 0.42, 1e-6);
    CHECK_NEAR(SanitizeSmoothing(1.0f, 0.15f), 1.0, 1e-6);
    CHECK_NEAR(SanitizeSmoothing(1.5f, 0.15f), 1.0, 1e-6);
    CHECK_NEAR(SanitizeSmoothing(-0.5f, 0.15f), 0.0, 1e-6);
    CHECK_NEAR(SanitizeSmoothing(nan, 0.15f), 0.15, 1e-6);
    CHECK_NEAR(SanitizeSmoothing(inf, 0.0f), 0.0, 1e-6);
    return failures;
}

int FovScaleTests() {
    int failures = 0;
    CHECK_NEAR(SanitizeFovScale(1.0f), 1.0, 1e-6);
    CHECK_NEAR(SanitizeFovScale(0.1f), 0.5, 1e-6);
    CHECK_NEAR(SanitizeFovScale(9.0f), 2.0, 1e-6);
    CHECK_NEAR(SanitizeFovScale(std::numeric_limits<float>::quiet_NaN()), 1.0, 1e-6);
    return failures;
}

int PositionLimitTests() {
    int failures = 0;
    // A wider range than the shipped default is a legitimate choice, so magnitude
    // passes through; a negative one would invert the clamp bounds, so it does not.
    CHECK_NEAR(SanitizePositionLimit(0.35f, 0.30f), 0.35, 1e-6);
    CHECK_NEAR(SanitizePositionLimit(99.0f, 0.30f), 99.0, 1e-6);
    CHECK_NEAR(SanitizePositionLimit(-0.5f, 0.30f), 0.0, 1e-6);
    CHECK_NEAR(SanitizePositionLimit(std::numeric_limits<float>::quiet_NaN(), 0.30f),
               0.30, 1e-6);
    return failures;
}

int CollisionPaddingTests() {
    int failures = 0;
    CHECK_NEAR(SanitizeCollisionPadding(25.0f, 10.0f, 100.0f), 25.0, 1e-6);
    CHECK_NEAR(SanitizeCollisionPadding(0.0f, 10.0f, 100.0f), 0.0, 1e-6);
    // A negative clearance would stop the view PAST the surface it is avoiding.
    CHECK_NEAR(SanitizeCollisionPadding(-5.0f, 10.0f, 100.0f), 0.0, 1e-6);
    // A clearance wider than any lean cancels positional tracking outright.
    CHECK_NEAR(SanitizeCollisionPadding(4000.0f, 10.0f, 100.0f), 100.0, 1e-6);
    CHECK_NEAR(SanitizeCollisionPadding(std::numeric_limits<float>::quiet_NaN(), 10.0f,
                                        100.0f), 10.0, 1e-6);
    return failures;
}

}  // namespace

int RunConfigSanitizeTests() {
    std::printf("Config sanitize\n");
    int failures = SmoothingTests();
    failures += FovScaleTests();
    failures += PositionLimitTests();
    failures += CollisionPaddingTests();
    return failures;
}
