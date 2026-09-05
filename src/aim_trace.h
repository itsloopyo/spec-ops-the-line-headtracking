#pragma once

#include <cstdint>

namespace SpecOpsTheLineHeadTracking {

// Watches the world's line checks for the one the game runs along the player's aim, so
// the reticle can be projected at the POINT the shot lands on rather than along its
// direction. Without a distance a positional lean slides the reticle off the thing it is
// marking, by roughly the lean divided by that distance.
//
// singleLineCheckAddr is UWorld::SingleLineCheck; worldPtrAddr is the GWorld pointer it
// is called on.
bool InstallAimTrace(std::uintptr_t singleLineCheckAddr, std::uintptr_t worldPtrAddr);

// How far along the aim the surface the shot stops on is, in engine units (centimetres).
//
// Casts the game's own crosshair ray - same source actor, flags and range, captured from
// the game running it - from the clean eye along the clean aim, on the frame that
// consumes the answer. No smoothing and no rate limit: the reticle is glued to a
// surface, so when the aim crosses an edge the impact point genuinely jumps and the
// reticle must jump with it.
//
// Returns false before the game has run its own crosshair trace once, when the ray hit
// nothing, or when the result could not be read. The caller then projects the aim
// direction, which is the right answer for a target at infinity.
bool AimDistance(const float eye[3], const float fwd[3], float* outUnits);

// How far the view may travel from the clean camera along `dir` before it reaches a
// surface, in engine units, so a positional lean can be shortened rather than pushed
// through a wall. `dir` is a unit vector and `length` the distance to probe.
//
// Casts the same ray the crosshair is resolved along - the game's own trace flags, and
// the player's own pawn as the actor to ignore - so it stops on what the level is built
// from. Returns false when nothing was hit inside `length`, and false as well before the
// game has run its own crosshair check once, because that is where the flags and the
// actor come from.
bool TraceClearance(const float start[3], const float dir[3], float length,
                    float* outUnits);

// Drops the captured trace parameters. The source actor is the player's own pawn, so a
// level load, a death or a chapter change frees it; the camera hook calls this the
// moment gameplay stops, and the next cast waits for the game to run its own crosshair
// check again rather than handing the engine a dangling actor.
void InvalidateAimTrace();

void RemoveAimTrace();

// Times a line check was seen starting at the camera. Zero means the game runs no trace
// from the eye, and the distance has to come from somewhere else.
unsigned long AimTraceMatchCount();

// Times a distance was actually produced. Compared with the draw count it separates "the
// ray never runs" from "the ray runs and hits nothing".
unsigned long AimTraceHitCount();

}  // namespace SpecOpsTheLineHeadTracking
