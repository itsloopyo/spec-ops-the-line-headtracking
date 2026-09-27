#include "config.h"

#include "legacy_config/legacy_config.h"
#include "path_utils.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <string>
#include <utility>
#include <vector>

namespace SpecOpsTheLineHeadTracking {

namespace {

using cameraunlock::config::DropRule;
using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::LegacyInput;
using cameraunlock::config::LegacyPoseShaping;
using cameraunlock::config::PoseShapingValue;
using cameraunlock::input::KeyModifiers;

// The range the published build clamped [FieldOfView] Scale to.
constexpr float kMinFovScale = 0.5f;
constexpr float kMaxFovScale = 2.0f;

// A legacy hotkey code and its Ctrl+Shift chord switch as one key list: the code's binding,
// then the chord.
std::string KeyList(int vk, bool chord, char letter, const char* key, std::vector<DroppedValue>& dropped) {
    std::string list = cameraunlock::config::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    if (chord) {
        const std::string chordKey =
            cameraunlock::input::FormatKeyBindings({{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
        list += (list.empty() ? "" : ", ") + chordKey;
    }
    return list;
}

ImportResult Import(const LegacyInput& input, Config& out) {
    // The published build opened the file by the ANSI path it built itself, not the one the
    // owner derives, so the import builds it the same way.
    const std::string ansiPath = LegacyAnsiPath(input.path);
    if (ansiPath.empty()) {
        return ImportResult::Refused(
            "its folder has no ANSI form, so the version that wrote this file could not open it and did "
            "not start");
    }

    legacy::Config c;
    const legacy::ReadResult read = c.Read(ansiPath.c_str());
    if (read.status == legacy::ReadStatus::Refused) {
        return ImportResult::Refused(read.reason);
    }

    std::vector<DroppedValue> dropped;
    std::vector<PoseShapingValue> shaping;

    out.enable_on_startup = c.enabled_on_startup;
    out.udp_port = c.udp_port;
    out.data_freshness_ms = c.data_freshness_ms;
    out.world_space_yaw = c.world_space_yaw;

    // [Position] Enabled chose only the startup mode: the cycle key reached every mode
    // either way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                           : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    out.local_smoothing = c.local_smoothing;
    out.position.local_smoothing = c.local_smoothing;
    out.remote_smoothing = c.remote_smoothing;
    out.position.remote_smoothing = c.remote_smoothing;

    out.position.limit_x = c.pos_limit_x;
    out.position.limit_y = c.pos_limit_y;
    out.position.limit_y_down = c.pos_limit_y_down;
    out.position.limit_z = c.pos_limit_z;
    out.position.limit_z_back = c.pos_limit_z_back;

    out.collision_enabled = c.collision_enabled;
    out.lean_clamp.skin = c.collision_padding;

    out.fov_scale = c.fov_scale;

    // The unit scale shipped at the centimetres per metre the camera hook now applies itself.
    // The build already ignored every sensitivity, deadzone and inversion key, so none of them
    // is carried.
    LegacyPoseShaping(c.position_scale, kWorldUnitsPerMetre, "Position", "PositionScale", shaping, dropped);

    // The aim marker is a diagnostic with no setting: it stays off.
    if (c.show_aim_marker) dropped.push_back({DropRule::Reticle, "General", "ShowAimMarker", "true"});

    out.toggle_key_name = KeyList(c.vk_toggle, c.chord_toggle, 'Y', "Toggle", dropped);
    out.cycle_tracking_mode_key_name = KeyList(c.vk_cycle_mode, c.chord_cycle_mode, 'G', "CycleMode", dropped);
    out.yaw_mode_key_name = KeyList(c.vk_yaw_mode, c.chord_yaw_mode, 'H', "YawMode", dropped);

    // A setting the player never changed from what the published build shipped follows
    // Defaults.ini, and each hotkey is its code and its chord switch together.
    using cameraunlock::config::schema::Concept;
    const legacy::Config shipped;
    cameraunlock::config::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, c.udp_port, shipped.udp_port);
    follows.Setting(Concept::EnableOnStartup, c.enabled_on_startup, shipped.enabled_on_startup);
    follows.Setting(Concept::DataFreshnessMs, c.data_freshness_ms, shipped.data_freshness_ms);
    follows.Setting(Concept::WorldSpaceYaw, c.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(c.position_enabled, shipped.position_enabled);
    follows.Setting(Concept::LocalSmoothing, c.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, c.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(Concept::PositionLimitX, c.pos_limit_x, shipped.pos_limit_x);
    follows.Setting(Concept::PositionLimitY, c.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(Concept::PositionLimitYDown, c.pos_limit_y_down, shipped.pos_limit_y_down);
    follows.Setting(Concept::PositionLimitZ, c.pos_limit_z, shipped.pos_limit_z);
    follows.Setting(Concept::PositionLimitZBack, c.pos_limit_z_back, shipped.pos_limit_z_back);
    follows.Setting(Concept::CollisionEnabled, c.collision_enabled, shipped.collision_enabled);
    follows.Setting(Concept::ToggleKey, c.vk_toggle == shipped.vk_toggle && c.chord_toggle == shipped.chord_toggle);
    follows.Setting(Concept::CycleTrackingModeKey,
                    c.vk_cycle_mode == shipped.vk_cycle_mode && c.chord_cycle_mode == shipped.chord_cycle_mode);
    follows.Setting(Concept::YawModeKey, c.vk_yaw_mode == shipped.vk_yaw_mode && c.chord_yaw_mode == shipped.chord_yaw_mode);

    return read.status == legacy::ReadStatus::Absent
               ? ImportResult::Absent(std::move(dropped), std::move(shaping), follows.Concepts())
               : ImportResult::Imported(std::move(dropped), std::move(shaping), follows.Concepts());
}

}  // namespace

cameraunlock::config::ConfigTable<Config> MakeConfigTable() {
    using cameraunlock::config::schema::Concept;
    cameraunlock::config::ConfigTable<Config> table = cameraunlock::config::HeadTrackingConfigTable<Config>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::DataFreshnessMs, Concept::WorldSpaceYaw,
         Concept::RotationEnabled, Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionEnabled,
         Concept::PositionLimitX, Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ,
         Concept::PositionLimitZBack, Concept::CollisionEnabled, Concept::CollisionMargin, Concept::ToggleKey,
         Concept::CycleTrackingModeKey, Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Select(Concept::CollisionMargin)
        .Comment("How far, in centimetres, the view is held off a wall when you lean into it.");
    table.Local("FieldOfView", "FovScale", &Config::fov_scale,
                cameraunlock::config::FloatCodec{kMinFovScale, kMaxFovScale},
                "Multiplies the field of view the game renders with: 0.5 to 2.0. 1.0 leaves it alone;\n"
                "the game draws 72 degrees horizontally from the hip, so 1.25 gives 90. Only the\n"
                "picture widens: the game keeps its own value for everything it decides with, so\n"
                "shots land exactly where they did. Aiming down sights is scaled by the same factor,\n"
                "so the sights keep their relative zoom.");
    return table;
}

cameraunlock::config::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults) {
    const auto wide = [](const char* name) { return std::wstring(name, name + std::char_traits<char>::length(name)); };
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = folder + wide(kConfigFileName);
    options.legacy_path = folder + wide(kLegacyConfigFileName);
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace SpecOpsTheLineHeadTracking
