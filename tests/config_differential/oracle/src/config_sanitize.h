#pragma once

#include <cmath>

namespace SpecOpsTheLineHeadTracking {

// Boundary validation for floats read from the user-editable INI. The core
// library already finite-checks rotation values arriving over UDP
// (OpenTrackPacket::FiniteFloat); the same guarantee must hold for config
// values, which feed into the identical smoothing/quaternion math. A NaN/Inf
// from a malformed INI (e.g. "LocalSmoothing=nan") otherwise poisons the
// smoothed quaternion and the injected view matrix.

inline float SanitizeFinite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}

inline float ClampRange(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Smoothing must be finite and within [0,1]. Above 1 the speed lerp in
// CalculateSmoothingFactor (Lerp(50,0.1,smoothing)) goes negative, producing a
// negative interpolation factor and a view that extrapolates instead of
// settling. This is validation, not a floor: any value the user sets inside
// [0,1] reaches the processor untouched. Applies to both LocalSmoothing and
// RemoteSmoothing; `fallback` is that key's shipped default.
inline float SanitizeSmoothing(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, 1.0f);
}

// Field of view multiplier: finite, and bounded so the widened view stays inside the
// range the aim projection accepts. The game renders at 72 degrees horizontal from the
// hip and 50 down the sights, so 2.0 reaches 144 - already past what a flat screen
// shows without heavy distortion - and 0.5 reaches 25.
inline float SanitizeFovScale(float v) {
    return ClampRange(SanitizeFinite(v, 1.0f), 0.5f, 2.0f);
}

// Camera collision clearance in world units (centimetres): finite, and non-negative
// because a negative one would place the stopping point PAST the surface, which is the
// thing the clamp exists to prevent. Bounded above as well - a clearance wider than the
// lean it is subtracted from cancels every lean, and a silent loss of positional
// tracking is a worse outcome than a value the log says was rejected.
inline float SanitizeCollisionPadding(float v, float fallback, float maxUnits) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, maxUnits);
}

// Position limit in metres: finite, and non-negative because the sign is not a
// tuning choice. PositionProcessor::ClampToLimits calls Clamp(v, -limit, +limit); a
// negative limit hands it lo > hi, which pins the offset at a constant instead of
// bounding it. Magnitude is left alone - a wider range than the shipped default is a
// legitimate choice, and the README quotes no ceiling.
inline float SanitizePositionLimit(float v, float fallback) {
    const float f = SanitizeFinite(v, fallback);
    return f < 0.0f ? 0.0f : f;
}

}
