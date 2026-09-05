#pragma once

namespace SpecOpsTheLineHeadTracking {

// Hooks the game's D3D9 device so the mod can see the presented image.
//
// It always publishes the presented image's size, which is what the crosshair hook
// scales its offset by. With drawAimMarker set it also draws the mod's own marker at the
// aim point, on top of the game's frame - a diagnostic, since the game's own crosshair is
// moved onto that same point.
//
// Hooks IDirect3D9::CreateDevice at install time and the device's EndScene once the game
// creates it.
bool InstallReticleOverlay(bool drawAimMarker);

// True once the game has created its device and the draw hook is live. False for the
// whole session means the overlay armed but the device was created some other way, and
// the marker will never appear - which is worth saying out loud rather than leaving as
// a silently missing feature.
bool IsReticleOverlayAttached();

void RemoveReticleOverlay();

}  // namespace SpecOpsTheLineHeadTracking
