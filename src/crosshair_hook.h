#pragma once

#include <cstdint>

namespace SpecOpsTheLineHeadTracking {

// Moves the game's own crosshair onto the aim point the camera hook publishes.
//
// Head tracking moves the rendered view without moving the aim, so the crosshair the HUD
// draws at screen centre no longer marks where rounds go. This detours the crosshair
// draw and offsets it by the same projection the aim marker uses.
//
// crosshairDrawAddr is UYCrosshair::Draw(AYHUD*, FLOAT), the one function every
// crosshair class goes through on its way to its own DrawCrosshair override.
bool InstallCrosshairHook(std::uintptr_t crosshairDrawAddr);

void RemoveCrosshairHook();

// Times the crosshair drew. Zero while the game draws no crosshair, which is most of the
// time outside combat, so it separates "the hook is dead" from "there is nothing to
// move" in a report that the crosshair did not follow.
unsigned long CrosshairDrawCount();

// Times it was drawn moved onto the aim point. Below the draw count by the frames where
// tracking was not being injected, and the crosshair was already on the shot.
unsigned long CrosshairMovedCount();

// Draws skipped because the aim point could not be placed on the frame - behind the
// camera, off the edge, or with no usable field of view. Separates "the crosshair is
// gone" from "the hook never ran".
unsigned long CrosshairSuppressedCount();

}  // namespace SpecOpsTheLineHeadTracking
