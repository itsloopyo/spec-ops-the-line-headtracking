#pragma once

#include <atomic>

namespace SpecOpsTheLineHeadTracking {

// Where the game's aim direction lands in the head-tracked view.
//
// The camera hook injects head rotation into the scene view only, so the game keeps
// aiming along the rotation the mouse chose while the player looks somewhere else.
// The reticle therefore has to leave screen centre. The hook publishes the clean aim
// direction resolved in the tracked view's own basis; the overlay turns that into a
// screen position with the backbuffer it is actually drawing into.
//
// Written on the game thread inside the scene-view hook and read on the render thread
// in the D3D9 present hook, so the components are atomic. They are deliberately not
// updated as one transaction: a torn read costs one frame of a few pixels on a marker
// that is already moving, and a lock on the render path costs more than that.
struct AimMarker {
    // Clean aim direction expressed in the tracked view basis: right, up, forward.
    std::atomic<float> right{0.0f};
    std::atomic<float> up{0.0f};
    std::atomic<float> forward{1.0f};
    // Horizontal field of view of the rendered view, in degrees, after any
    // FieldOfView.Scale - the angle the frame was actually projected with.
    std::atomic<float> fov_deg{75.0f};
    // The aspect ratio the scene view's projection was built with, when the game
    // constrains it to a fixed one, or 0 when the projection took the presented image's
    // own ratio. A constrained projection is letterboxed inside the presented image, so
    // this decides both the vertical field of view and the rectangle the projection's
    // normalised coordinates span.
    std::atomic<float> projection_aspect{0.0f};
    // Distance to the surface the shot stops on, in engine units, or 0 when the aim ray
    // hit nothing and the direction was projected instead. The hook folds it into the
    // direction above rather than the overlay applying it, so this is published for
    // diagnostics only.
    std::atomic<float> distance{0.0f};
    // The positional lean the hook applied this frame, in the engine's own units
    // (centimetres), along the clean camera's right, up and forward axes. Published
    // beside the direction it produced so one diagnostic line carries both terms of the
    // parallax correction.
    std::atomic<float> lean_right{0.0f};
    std::atomic<float> lean_up{0.0f};
    std::atomic<float> lean_forward{0.0f};
    // The clean camera's world position this frame, in engine units. Published so the
    // aim trace can recognise the game's own crosshair line check among the hundreds of
    // others the world runs per frame: that one starts at the camera.
    std::atomic<float> clean_x{0.0f};
    std::atomic<float> clean_y{0.0f};
    std::atomic<float> clean_z{0.0f};
    // Size of the image the frame was presented at, published by the overlay because it
    // is the only part of the mod that sees the real backbuffer. The crosshair hook
    // needs it to turn the projection into the pixels the HUD draws in.
    std::atomic<float> render_width{0.0f};
    std::atomic<float> render_height{0.0f};
    // False when tracking is not being injected this frame, so the overlay draws
    // nothing and the game's own centred crosshair is the only marker on screen.
    std::atomic<bool> active{false};
};

AimMarker& GetAimMarker();

}  // namespace SpecOpsTheLineHeadTracking
