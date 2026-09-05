#include "game_state.h"

#include "logging.h"

#include <cstddef>

namespace SpecOpsTheLineHeadTracking {

namespace {

// AWorldInfo is Actors(0) of the persistent level. Read off AWorldInfo::execIsMenuLevel
// @ 0x0059D880, which walks exactly this chain: GWorld, +0x50 the level, +0x3C the
// actor array's data pointer, element 0.
constexpr std::size_t kWorldPersistentLevel = 0x50;
constexpr std::size_t kLevelActors = 0x3C;

// AWorldInfo::Pauser, the PlayerReplicationInfo that requested the pause. Null in
// gameplay, set the moment the pause menu opens or the window loses focus. Measured in
// process by dumping AWorldInfo either side of an Escape press; the three floats below
// it at +0x454/+0x458/+0x45C corroborate the layout - across the same pair of dumps
// +0x458 advanced by exactly the wall-clock gap (RealTimeSeconds) while +0x454 stalled
// (TimeSeconds), which is what a pause does to those two.
constexpr std::size_t kWorldInfoPauser = 0x46C;

// AController::Pawn, the pawn this controller possesses. Null in the front end, non-null
// once a level is live: measured null in the main menu in three separate sessions and
// non-null in gameplay in all of them. Native code that reads it treats the result as an
// actor (0x0084FF97 compares a float at +0xF0 on it), which is the shape expected.
//
// This is what excludes the main menu, which has a player controller and renders a scene
// view but possesses nothing, and level transitions, where the chain above is null.
constexpr std::size_t kControllerPawn = 0x1E0;

// APlayerController::IgnoreLookInput, the byte counter UnrealScript's IgnoreLookInput()
// raises and lowers. SetCinematicMode raises it for the duration of a cutscene, which is
// the state this gate is after: the game has taken the camera, so a head pose would
// fight a scripted shot and the aim marker would claim a shot that cannot be taken.
//
// Read off APlayerController::IsLookInputIgnored, the virtual at vtable slot +0x4B0 that
// execIsLookInputIgnored (0x00C0C3E0, registered on both APlayerController and
// AYPlayerController) dispatches to. Its whole body is
// `xor al,al; cmp al,[ecx+0x3AE]; sbb eax,eax; neg eax; ret`, so the byte is the return
// value. All four PlayerController-family vtables in .rdata carry that same
// implementation at that same slot. IgnoreMoveInput sits one byte below at +0x3AD, which
// is the declaration order in PlayerController.uc and corroborates the pair.
//
// Look, not move: a scripted walk can lock movement while the player still turns freely,
// and tracking should survive that. Losing the look axis is what makes a frame not the
// player's to look around in.
//
// Measured in process across two sessions: the byte reads 2 through the opening
// cinematic and drops to 0 at the instant the game starts drawing its crosshair again
// (13:55:46.014 and 13:55:46.551 in one log, the same pair 2s apart in the other). The
// crosshair is the game's own statement that the player has the camera, so the two
// agreeing is what makes this the gameplay boundary and not just a flag that moves.
//
// It is a counter, not a flag - it rests at 2, one for each of the two script calls that
// raised it - so the test is against zero rather than against 1.
constexpr std::size_t kControllerIgnoreLookInput = 0x3AE;

std::uintptr_t g_worldPtrAddr = 0;

const void* Deref(const void* p, std::size_t offset) {
    if (!p) {
        return nullptr;
    }
    return *reinterpret_cast<const void* const*>(static_cast<const std::uint8_t*>(p) + offset);
}

}  // namespace

void InitGameState(std::uintptr_t worldPtrAddr) {
    g_worldPtrAddr = worldPtrAddr;
    Log::Line("Gameplay gate armed: GWorld @ 0x%p", reinterpret_cast<void*>(worldPtrAddr));
}

const char* Describe(GameplayState state) {
    switch (state) {
        case GameplayState::Playing: return "playing";
        case GameplayState::NoPawn:  return "no pawn possessed (menu or level transition)";
        case GameplayState::NoWorld: return "no world (loading)";
        case GameplayState::Paused:  return "paused (pause screen, or the window lost focus)";
        case GameplayState::Cinematic: return "cinematic (the game is ignoring look input)";
    }
    return "unknown";
}

GameplayState GetGameplayState(const void* playerController) {
    if (!Deref(playerController, kControllerPawn)) {
        return GameplayState::NoPawn;
    }

    const void* world = *reinterpret_cast<const void* const*>(g_worldPtrAddr);
    const void* actors = Deref(Deref(world, kWorldPersistentLevel), kLevelActors);
    const void* worldInfo = Deref(actors, 0);
    if (!worldInfo) {
        return GameplayState::NoWorld;
    }

    if (Deref(worldInfo, kWorldInfoPauser)) {
        return GameplayState::Paused;
    }

    // After the pause test on purpose: a cutscene that the player pauses is both, and
    // "paused" is the report that matches what they are looking at.
    if (*(static_cast<const std::uint8_t*>(playerController) + kControllerIgnoreLookInput)) {
        return GameplayState::Cinematic;
    }
    return GameplayState::Playing;
}

}  // namespace SpecOpsTheLineHeadTracking
