#pragma once

#include "aim_marker.h"

#include <cmath>

namespace SpecOpsTheLineHeadTracking {

enum class AimProjection {
    Ok,
    Inactive,
    Behind,
    BadFov,
    NotFinite,
};

// The rectangle the rendered frame occupies inside the presented image, in the
// presented image's pixels.
struct ViewRect {
    float x, y, width, height;
};

constexpr float kDegToRadians = 3.14159265358979323846f / 180.0f;

// The aim has swung more than 84 degrees off the rendered view, which is outside any
// frame this game draws. The guard is on the forward component APPROACHING zero rather
// than merely going negative: the projection diverges either side of it, and a marker at
// 1e30 is a NaN on its way to a vertex buffer.
//
// The guard is applied to the forward component DIVIDED BY the vector's length, so it is
// an angle whatever the caller publishes. Comparing the raw component against it only
// works for a unit direction: once a hit distance is folded in, the component is the
// distance in centimetres and the same 0.1 admits everything up to a hundredth of a
// degree off the view plane - which reaches the crosshair as an offset of millions of
// pixels.
constexpr float kMinForward = 0.1f;

// Field-of-view bounds the frame could plausibly have been projected with. Outside them
// the published angle is not the frame's and the marker would be placed by arithmetic on
// a number that means nothing.
constexpr float kMinFovDeg = 20.0f;
constexpr float kMaxFovDeg = 170.0f;

// Spec Ops constrains its scene view to a fixed aspect ratio (16:9 on the builds
// measured), so at any other resolution the frame is letterboxed and centred inside the
// window: measured at 1280x800, the world is drawn into a 1280x720 band 40 pixels down,
// and the game's own crosshair rests at the middle of THAT band, not of the window.
// Normalised device coordinates therefore span this rectangle. It degenerates to the
// whole image when the game leaves the projection on the window's own ratio, which is
// the case whenever the two agree.
inline ViewRect ViewRectFor(float imageWidth, float imageHeight) {
    const float aspect = GetAimMarker().projection_aspect.load(std::memory_order_relaxed);
    if (!std::isfinite(aspect) || aspect <= 0.0f) {
        return { 0.0f, 0.0f, imageWidth, imageHeight };
    }
    if (imageWidth > imageHeight * aspect) {
        const float w = imageHeight * aspect;
        return { (imageWidth - w) * 0.5f, 0.0f, w, imageHeight };
    }
    const float h = imageWidth / aspect;
    return { 0.0f, (imageHeight - h) * 0.5f, imageWidth, h };
}

// Where the clean aim direction lands in the head-tracked view, in normalised device
// coordinates: x right, y up, -1 to 1 across the drawn image.
//
// ONE derivation, used by both the aim marker overlay and the crosshair reposition. Two
// copies of this arithmetic is how a reticle and the marker beside it end up agreeing on
// single-axis poses and disagreeing on combined ones.
//
// CalcSceneView builds FPerspectiveMatrix(FOV * 0.5 * PI/180, Width, Height, NearZ), so
// the angle it is handed is half the HORIZONTAL field of view and the vertical
// half-angle follows from Width/Height. Pass the ratio of the rendered rectangle from
// ViewRectFor, not the window's: those differ whenever the game letterboxes, and the
// difference is invisible at screen centre and grows with the offset - which is the
// half of the frame the reticle only ever occupies when it matters.
inline AimProjection ProjectAim(float widthOverHeight, float* ndcX, float* ndcY) {
    const AimMarker& marker = GetAimMarker();
    // Acquire, paired with the release in PublishAimMarker: the components below are
    // only guaranteed to be this frame's once `active` has been observed set.
    if (!marker.active.load(std::memory_order_acquire)) {
        return AimProjection::Inactive;
    }

    const float fwd = marker.forward.load(std::memory_order_relaxed);
    const float right = marker.right.load(std::memory_order_relaxed);
    const float up = marker.up.load(std::memory_order_relaxed);
    const float length = std::sqrt(fwd * fwd + right * right + up * up);
    if (!std::isfinite(length) || length <= 0.0f) {
        return AimProjection::NotFinite;
    }
    if (!(fwd / length > kMinForward)) {
        return AimProjection::Behind;
    }

    const float fov = marker.fov_deg.load(std::memory_order_relaxed);
    if (!std::isfinite(fov) || fov < kMinFovDeg || fov > kMaxFovDeg) {
        return AimProjection::BadFov;
    }

    const float tanH = std::tan(fov * 0.5f * kDegToRadians);
    const float tanV = tanH / widthOverHeight;
    *ndcX = (right / fwd) / tanH;
    *ndcY = (up / fwd) / tanV;
    if (!std::isfinite(*ndcX) || !std::isfinite(*ndcY)) {
        return AimProjection::NotFinite;
    }
    return AimProjection::Ok;
}

}  // namespace SpecOpsTheLineHeadTracking
