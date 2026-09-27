#pragma once

#include <cstdint>

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace SpecOpsTheLineHeadTracking {

// The shipped defaults, in one place. WriteDefaultIni writes these, LoadOrCreate
// falls back to them, and Config's members are initialised from them, so a
// default-constructed Config, a freshly written INI and a read of a key that is
// missing from the file cannot disagree about what the default is.
namespace defaults {
constexpr bool  kEnableOnStartup = true;
constexpr int   kPort            = 4242;
constexpr int   kMinPort         = 1024;
constexpr int   kMaxPort         = 65535;
constexpr int   kDataFreshnessMs = 500;
constexpr bool  kWorldSpaceYaw   = true;
constexpr bool  kShowAimMarker   = false;
constexpr float kFovScale        = 1.0f;
constexpr float kLocalSmoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kRemoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);
constexpr int   kVkToggle        = 0x23; // VK_END
constexpr int   kVkCycleMode     = 0x21; // VK_PRIOR (Page Up)
constexpr int   kVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr bool  kChord           = true;

constexpr bool  kPositionEnabled = true;
constexpr float kPosLimitX       = cameraunlock::PositionSettings{}.limit_x;
constexpr float kPosLimitY       = cameraunlock::PositionSettings{}.limit_y;
constexpr float kPosLimitYDown   = cameraunlock::PositionSettings{}.limit_y_down;
constexpr float kPosLimitZ       = cameraunlock::PositionSettings{}.limit_z;
constexpr float kPosLimitZBack   = cameraunlock::PositionSettings{}.limit_z_back;
constexpr float kPositionScale   = 100.0f;

constexpr bool  kCollisionEnabled = true;
// World units, and the world is centimetres. Ten of them is close enough to a wall to
// lean out from behind it and far enough that the near clip plane has nothing to cut
// into.
constexpr float kCollisionPadding = 10.0f;
constexpr float kMaxCollisionPadding = 100.0f;
}  // namespace defaults

struct Config {
    bool  enabled_on_startup = defaults::kEnableOnStartup;
    uint16_t udp_port = static_cast<uint16_t>(defaults::kPort);

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine (loopback) uses local_smoothing, a remote network
    // device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = defaults::kLocalSmoothing;
    float remote_smoothing = defaults::kRemoteSmoothing;

    // Draw the mod's own marker at the aim point as well. Off by default: the game's own
    // crosshair is moved onto that point, so the marker is a diagnostic.
    bool show_aim_marker = defaults::kShowAimMarker;
    int  data_freshness_ms = defaults::kDataFreshnessMs;

    // true = horizon-locked (world-space) yaw, false = camera-local yaw.
    bool world_space_yaw = defaults::kWorldSpaceYaw;

    // Multiplies the field of view the scene view is projected with. 1.0 is the game's
    // own. Only the rendered image widens: the two game-logic callers of the same
    // accessor keep the game's value, and the reticle projection reads the number the
    // frame was actually projected with, so it follows automatically.
    float fov_scale = defaults::kFovScale;

    // 6DOF positional tracking.
    bool  position_enabled = defaults::kPositionEnabled;
    float pos_limit_x = defaults::kPosLimitX;
    // Vertical travel is clamped as [-pos_limit_y_down, +pos_limit_y]. The two are
    // separate keys because a player sitting down has less room to duck than to
    // stretch up, and mirroring one into the other would hide that.
    float pos_limit_y = defaults::kPosLimitY;
    float pos_limit_y_down = defaults::kPosLimitYDown;
    float pos_limit_z = defaults::kPosLimitZ;
    float pos_limit_z_back = defaults::kPosLimitZBack;
    // World units (cm) per metre of head translation. UE3 world is centimetres.
    float position_scale = defaults::kPositionScale;

    // Stop a lean at the level's geometry instead of letting the render eye travel
    // through it. The game collides its own camera before the head pose is added, so
    // nothing but this bounds where a lean puts the eye.
    bool  collision_enabled = defaults::kCollisionEnabled;
    // How far short of a surface the leaned view stops, in world units (centimetres).
    float collision_padding = defaults::kCollisionPadding;

    int vk_toggle     = defaults::kVkToggle;
    int vk_cycle_mode = defaults::kVkCycleMode;
    int vk_yaw_mode   = defaults::kVkYawMode;
    bool chord_toggle = defaults::kChord;
    bool chord_cycle_mode = defaults::kChord;
    bool chord_yaw_mode = defaults::kChord;

    bool LoadOrCreate(const char* iniPath);
};

}
