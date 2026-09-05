#pragma once

#include <cstdint>

namespace SpecOpsTheLineHeadTracking {

// Why the head pose is or is not being applied this frame.
enum class GameplayState {
    Playing,
    // The player controller possesses no pawn: the main menu, or a level transition.
    NoPawn,
    // The world chain is not resolvable, which is what a map load looks like mid-flight.
    NoWorld,
    // AWorldInfo::Pauser is set: the pause screen, or the window has lost focus.
    Paused,
    // The game is ignoring the player's look input: a cutscene, or another scripted
    // moment where it has taken the camera away from the player.
    Cinematic,
};

const char* Describe(GameplayState state);

// worldPtrAddr is the GWorld pointer the aim trace already uses; the gameplay test
// reaches AWorldInfo through it.
void InitGameState(std::uintptr_t worldPtrAddr);

// Reports whether the player is in gameplay, as opposed to a menu, a pause screen or a
// level transition. playerController is the AYPlayerController the scene-view builder is
// drawing for - the `this` the camera hook's detour receives.
//
// The scene view is built at the full frame rate in the main menu and behind the pause
// menu alike (measured: 121 render calls a second in both), so without this the head
// pose would keep swinging the camera around while the player is reading a menu.
//
// The reason is returned rather than a bool because the ways of not being in gameplay
// fail for different reasons, and a report that tracking stopped is only actionable if
// the log says which one.
GameplayState GetGameplayState(const void* playerController);

}  // namespace SpecOpsTheLineHeadTracking
