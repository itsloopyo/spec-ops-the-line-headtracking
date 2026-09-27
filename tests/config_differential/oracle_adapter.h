#pragma once

// The oracle: the config reader and hotkey registration of the newest published build (the
// rolling `dev` pre-release, 3e9a526), compiled from oracle/ with the core sources they
// included at its pin (660b1f8). Two libraries build it, each with its namespaces renamed at
// compile time so it links beside the current core: the reader, and Hotkeys::Start against
// oracle_fake's recording poller. This header names no core type, so the test includes it
// without the renaming.

#include <array>
#include <string>
#include <vector>

namespace specops_oracle_view {

struct OracleConfig {
    bool enabled_on_startup;
    int udp_port;
    float local_smoothing, remote_smoothing;
    bool show_aim_marker;
    int data_freshness_ms;
    bool world_space_yaw;
    float fov_scale;
    bool position_enabled;
    float pos_limit_x, pos_limit_y, pos_limit_y_down, pos_limit_z, pos_limit_z_back;
    float position_scale;
    bool collision_enabled;
    float collision_padding;
    int vk_toggle, vk_cycle_mode, vk_yaw_mode;
    bool chord_toggle, chord_cycle_mode, chord_yaw_mode;
};

struct OracleResult {
    // What Config::LoadOrCreate returned. false stopped the mod at startup.
    bool loaded;
    OracleConfig config;
};

// Config::LoadOrCreate on a default Config, as the published build's InitThread ran it.
// Creates the file when there is none, as that build did.
OracleResult RunOracle(const std::string& path);

// The hotkey codes and chord switches the published build's Hotkeys::Start registered from.
struct HotkeyView {
    int vk_toggle, vk_cycle_mode, vk_yaw_mode;
    bool chord_toggle, chord_cycle_mode, chord_yaw_mode;
};

// Which actions a key press fires, for every key a binding can name (0x01-0xFE) under every
// set of held modifiers. Entry (vk - kFirstKey) * kHeldStates + held counts the toggle,
// cycle and yaw mode actions fired, in that order. held: 1 Ctrl, 2 Shift, 4 Alt.
constexpr int kFirstKey = 0x01;
constexpr int kLastKey = 0xFE;
constexpr int kHeldStates = 8;
using FireTable = std::vector<std::array<int, 3>>;

// The published build's Hotkeys::Start run on `keys`, pressing each key under each held set.
FireTable OracleFires(const HotkeyView& keys);

}
