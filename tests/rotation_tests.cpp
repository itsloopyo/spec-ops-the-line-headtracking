#include "test_support.h"

#include "ue3_rotation.h"

#include <initializer_list>
#include <limits>

using namespace SpecOpsTheLineHeadTracking;

namespace {

UE3Rotator RotatorFromDegrees(float pitch, float yaw, float roll) {
    return { DegToUnits(pitch), DegToUnits(yaw), DegToUnits(roll) };
}

int UnitConversionTests() {
    int failures = 0;
    // 65536 FRotator units to the turn, so a quarter turn is 16384.
    CHECK(DegToUnits(90.0f) == 16384);
    CHECK(DegToUnits(-90.0f) == -16384);
    CHECK(DegToUnits(0.0f) == 0);
    CHECK_NEAR(UnitsToRad(16384), kPi / 2.0, 1e-4);
    CHECK(RadToUnits(static_cast<float>(kPi) / 2.0f) == 16384);
    return failures;
}

// DegToUnits sits on the per-frame injection path and its result is cast to int32. An
// INI Sensitivity has no bound on magnitude, so the product reaching it can leave that
// range or be non-finite, and the cast is undefined for both. An FRotator axis is
// modular, so the in-range answers must be untouched and the out-of-range ones must wrap
// into one revolution instead of overflowing.
int DegToUnitsBoundsTests() {
    int failures = 0;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    CHECK(DegToUnits(nan) == 0);
    CHECK(DegToUnits(inf) == 0);
    CHECK(DegToUnits(-inf) == 0);

    // Angles the int32 could never have held come back inside one revolution rather
    // than as whatever the overflowing cast happened to produce.
    for (float deg : { 1e12f, -1e12f, 1e30f, -1e30f, 3.4e38f }) {
        const std::int32_t units = DegToUnits(deg);
        CHECK(units > -static_cast<std::int32_t>(kUnitsPerRevolution));
        CHECK(units < static_cast<std::int32_t>(kUnitsPerRevolution));
    }

    // Wrapping is exact for a whole number of turns, and one turn either side of an
    // angle is the same rotator position.
    CHECK(DegToUnits(360.0f) == 0);
    CHECK(DegToUnits(-360.0f) == 0);
    CHECK(DegToUnits(450.0f) == DegToUnits(90.0f));
    CHECK(DegToUnits(-450.0f) == DegToUnits(-90.0f));

    // Everything inside the range a tracker actually produces is bit-for-bit what it
    // was before the guard: 65536 units to the turn, rounded to nearest.
    CHECK(DegToUnits(180.0f) == 32768);
    CHECK(DegToUnits(-180.0f) == -32768);
    CHECK(DegToUnits(45.0f) == 8192);
    CHECK(DegToUnits(1.0f) == 182);
    CHECK(DegToUnits(-1.0f) == -182);
    return failures;
}

int IdentityTests() {
    int failures = 0;
    const Mat3 I = RotatorToMatrix(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            CHECK_NEAR(I.m[i][j], i == j ? 1.0 : 0.0, 1e-6);
        }
    }
    return failures;
}

// Row 0 is the world forward axis, which is what the aim marker projects.
int ForwardAxisTests() {
    int failures = 0;
    const Mat3 yawed = RotatorToMatrix(RotatorFromDegrees(0.0f, 90.0f, 0.0f));
    float f[3] = { 0.0f, 0.0f, 0.0f };
    ForwardAxis(yawed, f);
    CHECK_NEAR(f[0], 0.0, 1e-4);
    CHECK_NEAR(f[1], 1.0, 1e-4);
    CHECK_NEAR(f[2], 0.0, 1e-4);

    const Mat3 pitched = RotatorToMatrix(RotatorFromDegrees(30.0f, 0.0f, 0.0f));
    ForwardAxis(pitched, f);
    CHECK_NEAR(f[0], std::cos(30.0 * kPi / 180.0), 1e-4);
    CHECK_NEAR(f[1], 0.0, 1e-4);
    CHECK_NEAR(f[2], std::sin(30.0 * kPi / 180.0), 1e-4);
    return failures;
}

int RoundTripTests() {
    int failures = 0;
    const UE3Rotator cases[] = {
        RotatorFromDegrees(0.0f, 0.0f, 0.0f),
        RotatorFromDegrees(10.0f, 45.0f, 0.0f),
        RotatorFromDegrees(-25.0f, 170.0f, 12.0f),
        RotatorFromDegrees(60.0f, -120.0f, -30.0f),
    };
    for (const UE3Rotator& r : cases) {
        UE3Rotator back{};
        MatrixToRotator(RotatorToMatrix(r), &back);
        // One unit is 360/65536 degrees, so a couple of units is well inside float noise.
        CHECK(std::abs(back.Pitch - r.Pitch) <= 4);
        CHECK(std::abs(back.Yaw - r.Yaw) <= 4);
        CHECK(std::abs(back.Roll - r.Roll) <= 4);
    }
    return failures;
}

// FRotator axes are modular, so the game's own pitch arrives anywhere in int32 and the
// clamp has to fold it onto the half-turn either side of zero before comparing. Without
// that, 60000 units (-5.5 degrees) reads as far past vertical and gets pinned.
int PitchClampTests() {
    int failures = 0;

    CHECK(WrapSigned(0) == 0);
    CHECK(WrapSigned(16384) == 16384);
    // The half-open boundary: 180 degrees has one encoding, -32768, and the two branches
    // turn on exactly this pair.
    CHECK(WrapSigned(32767) == 32767);
    CHECK(WrapSigned(32768) == -32768);
    CHECK(WrapSigned(32769) == -32767);
    CHECK(WrapSigned(-32768) == -32768);
    CHECK(WrapSigned(-32769) == 32767);
    // Every wrap lands inside the half-open range.
    for (std::int32_t v : { -70000, -65537, -65536, -1, 1, 65535, 65537, 100000,
                            2147483647, -2147483647 - 1 }) {
        const std::int32_t w = WrapSigned(v);
        CHECK(w >= -32768 && w <= 32767);
    }
    CHECK(WrapSigned(60000) == 60000 - 65536);
    CHECK(WrapSigned(-60000) == 65536 - 60000);
    CHECK(WrapSigned(65536) == 0);
    CHECK(WrapSigned(70000) == 70000 - 65536);

    // Angles inside the normal range pass through untouched.
    CHECK(ClampPitch(DegToUnits(30.0f)) == DegToUnits(30.0f));
    CHECK(ClampPitch(DegToUnits(-30.0f)) == DegToUnits(-30.0f));
    CHECK(ClampPitch(0) == 0);

    // A head pitch stacked on an already-steep camera stops one unit short of vertical
    // rather than passing it and inverting the world.
    CHECK(ClampPitch(DegToUnits(80.0f) + DegToUnits(30.0f)) == kMaxPitchUnits);
    CHECK(ClampPitch(DegToUnits(-80.0f) + DegToUnits(-30.0f)) == -kMaxPitchUnits);
    CHECK(ClampPitch(32000) == kMaxPitchUnits);

    // A modular pitch that denotes a shallow angle is not mistaken for a steep one.
    CHECK(ClampPitch(60000) == 60000 - 65536);
    return failures;
}

// The camera hook's two yaw modes: horizon-locked adds per axis, camera-local composes
// M_head * M_clean. They must coincide when the clean camera is level, which is the
// property the mode toggle is documented on - and they must NOT be assumed to coincide
// once it is not, which is the case a level-only test cannot see.
int YawModeEquivalenceTests() {
    int failures = 0;
    const UE3Rotator clean = RotatorFromDegrees(0.0f, 35.0f, 0.0f);
    const UE3Rotator head = RotatorFromDegrees(0.0f, 20.0f, 0.0f);

    UE3Rotator composed{};
    MatrixToRotator(MatMul(RotatorToMatrix(head), RotatorToMatrix(clean)), &composed);

    CHECK(std::abs(composed.Yaw - (clean.Yaw + head.Yaw)) <= 4);
    CHECK(std::abs(composed.Pitch - clean.Pitch) <= 4);
    CHECK(std::abs(composed.Roll - clean.Roll) <= 4);

    // Head roll on a level camera is likewise a pure per-axis add.
    const UE3Rotator headRoll = RotatorFromDegrees(0.0f, 0.0f, 15.0f);
    MatrixToRotator(MatMul(RotatorToMatrix(headRoll), RotatorToMatrix(clean)), &composed);
    CHECK(std::abs(composed.Roll - headRoll.Roll) <= 4);
    CHECK(std::abs(composed.Yaw - clean.Yaw) <= 4);

    // With the clean camera PITCHED, the two modes genuinely differ - camera-local yaw
    // rotates about the tilted up-axis, which is the whole reason the toggle exists.
    // The composition must still be a valid rotation that round-trips.
    const UE3Rotator pitched = RotatorFromDegrees(40.0f, 35.0f, 0.0f);
    MatrixToRotator(MatMul(RotatorToMatrix(head), RotatorToMatrix(pitched)), &composed);
    CHECK(std::abs(composed.Yaw - (pitched.Yaw + head.Yaw)) > 4);

    const Mat3 direct = MatMul(RotatorToMatrix(head), RotatorToMatrix(pitched));
    const Mat3 roundTripped = RotatorToMatrix(composed);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            CHECK_NEAR(roundTripped.m[i][j], direct.m[i][j], 1e-3);
        }
    }
    return failures;
}

int ResolveInBasisTests() {
    int failures = 0;
    const Mat3 basis = RotatorToMatrix(RotatorFromDegrees(15.0f, 40.0f, 8.0f));

    // A vector along the basis's own forward resolves to pure forward.
    float f[3] = { 0.0f, 0.0f, 0.0f };
    ForwardAxis(basis, f);
    const float alongForward[3] = { f[0] * 250.0f, f[1] * 250.0f, f[2] * 250.0f };
    float fwd = 0.0f, right = 0.0f, up = 0.0f;
    ResolveInBasis(basis, alongForward, &fwd, &right, &up);
    CHECK_NEAR(fwd, 250.0, 1e-2);
    CHECK_NEAR(right, 0.0, 1e-2);
    CHECK_NEAR(up, 0.0, 1e-2);

    // Resolving in the identity basis is the identity.
    const Mat3 I = RotatorToMatrix(0.0f, 0.0f, 0.0f);
    const float v[3] = { 3.0f, -4.0f, 5.0f };
    ResolveInBasis(I, v, &fwd, &right, &up);
    CHECK_NEAR(fwd, 3.0, 1e-4);
    CHECK_NEAR(right, -4.0, 1e-4);
    CHECK_NEAR(up, 5.0, 1e-4);
    return failures;
}

}  // namespace

int RunRotationTests() {
    std::printf("UE3 rotation\n");
    int failures = UnitConversionTests();
    failures += DegToUnitsBoundsTests();
    failures += PitchClampTests();
    failures += IdentityTests();
    failures += ForwardAxisTests();
    failures += RoundTripTests();
    failures += YawModeEquivalenceTests();
    failures += ResolveInBasisTests();
    return failures;
}
