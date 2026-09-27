#include "legacy_config/legacy_config.h"

#include "legacy_config/config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <string>

namespace SpecOpsTheLineHeadTracking::legacy {

namespace {

// The shipped defaults of the frozen build, as its config.h spelled them.
constexpr bool  kEnableOnStartup = true;
constexpr int   kPort            = 4242;
constexpr int   kMinPort         = 1024;
constexpr int   kMaxPort         = 65535;
constexpr int   kDataFreshnessMs = 500;
constexpr bool  kWorldSpaceYaw   = true;
constexpr bool  kShowAimMarker   = false;
constexpr float kFovScale        = 1.0f;
constexpr float kLocalSmoothing  = static_cast<float>(0.0);
constexpr float kRemoteSmoothing = static_cast<float>(0.15);
constexpr int   kVkToggle        = 0x23; // VK_END
constexpr int   kVkCycleMode     = 0x21; // VK_PRIOR (Page Up)
constexpr int   kVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr bool  kChord           = true;

constexpr bool  kPositionEnabled = true;
constexpr float kPosLimitX       = 0.30f;
constexpr float kPosLimitY       = 0.20f;
constexpr float kPosLimitZ       = 0.40f;
constexpr float kPosLimitZBack   = 0.10f;
constexpr float kPositionScale   = 100.0f;

constexpr bool  kCollisionEnabled = true;
constexpr float kCollisionPadding = 10.0f;
constexpr float kMaxCollisionPadding = 100.0f;

// Reads a float and runs it through the boundary check for its key, warning when the
// file held something the mod will not use. One place where the read, the check and the
// report meet, so a key cannot end up reported under another key's name or written into
// the struct without having been checked at all.
template <typename Sanitizer>
float ReadSanitized(const cameraunlock::IniReader& ini, const char* section,
                    const char* key, float fallback, Sanitizer clean) {
    const float raw = ini.ReadFloat(section, key, fallback);
    const float value = clean(raw);
    if (raw != value) {
        Log::Line("WARN: INI %s.%s value %.4f out of range or non-finite; using %.4f",
                  section, key, raw, value);
    }
    return value;
}

// The four [Position] limits share one boundary check, whose NaN fallback is the key's
// own shipped default, so each of them is one line at the call site.
float ReadPositionLimit(const cameraunlock::IniReader& ini, const char* key,
                        float fallback) {
    return ReadSanitized(ini, "Position", key, fallback,
                         [fallback](float v) { return SanitizePositionLimit(v, fallback); });
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "WARN: Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

// The pose-shaping keys, retired together. The tracker owns pose shaping: sensitivity,
// deadzone and axis inversion are configured once in OpenTrack or the phone app, where
// one profile then behaves the same in every game, instead of per-game here. Silently
// ignoring a key an existing INI still sets would leave the user adjusting a number that
// does nothing, so each one is named once.
//
// Protocol-to-engine sign conversion is NOT one of these and stays in the camera hook,
// where it belongs: that is a fact about UE3, not a preference.
// The recentre binding, retired earlier. The mod keeps no centre of its own: it applies
// whatever pose the tracker sends, the way a TrackIR driver's game support does. Two
// centres in series drift apart, because each side recentres at moments the other cannot
// see, and the player then needs two presses for one recentre. An INI that still binds a
// key here would silently bind nothing.
void WarnRetiredRecenterKeys(const cameraunlock::IniReader& reader) {
    static bool warned = false;
    if (warned) return;
    for (const char* key : { "Recenter", "ChordRecenter" }) {
        if (reader.ReadString("Hotkeys", key, "").empty()) {
            continue;
        }
        warned = true;
        Log::Line("WARN: Config key [Hotkeys] %s has been retired and is IGNORED. The mod "
                  "keeps no centre of its own, so there is nothing for it to bind - "
                  "recentre in your tracker instead (opentrack's Center bind, or the "
                  "CENTER button in a phone app).", key);
        return;
    }
}

void WarnRetiredShapingKeys(const cameraunlock::IniReader& reader) {
    static const struct { const char* section; const char* key; } kRetired[] = {
        { "Sensitivity", "Yaw" },         { "Sensitivity", "Pitch" },
        { "Sensitivity", "Roll" },        { "Sensitivity", "InvertYaw" },
        { "Sensitivity", "InvertPitch" }, { "Sensitivity", "InvertRoll" },
        { "Smoothing",   "DeadzoneDeg" }, { "Position",    "SensitivityX" },
        { "Position",    "SensitivityY" },{ "Position",    "SensitivityZ" },
        { "Position",    "InvertX" },     { "Position",    "InvertY" },
        { "Position",    "InvertZ" },
    };
    static bool warned = false;
    if (warned) return;
    for (const auto& entry : kRetired) {
        if (reader.ReadString(entry.section, entry.key, "").empty()) {
            continue;
        }
        warned = true;
        Log::Line("WARN: Config key [%s] %s has been retired and is IGNORED, along with "
                  "the other sensitivity, deadzone and invert keys. The mod uses the "
                  "tracker's pose at 1:1 so one tracker profile behaves the same in "
                  "every game - set sensitivity, deadzones and axis inversion in "
                  "OpenTrack or your phone app instead.", entry.section, entry.key);
        return;
    }
}

// GetAsyncKeyState, which the poller polls these with, is defined for virtual-key codes
// 0x01-0xFE; 0 is the poller's own "this hotkey is unbound" sentinel. Anything else is a
// typo (an extra digit, a scan code pasted in place of a VK) that reaches the poller,
// polls nothing, and leaves the user with a key that silently never fires. Report it and
// keep the shipped binding rather than shipping a dead one.
constexpr int kMaxVirtualKey = 0xFE;

int ReadVirtualKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int vk = ini.ReadHex("Hotkeys", key, fallback);
    if (vk < 0 || vk > kMaxVirtualKey) {
        Log::Line("WARN: INI Hotkeys.%s value 0x%X is not a virtual-key code (0x01-0xFE, "
                  "or 0 to unbind); using 0x%02X", key, vk, fallback);
        return fallback;
    }
    return vk;
}

}

ReadResult Config::Read(const char* iniPath) {
    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        return {ReadStatus::Absent, {}};
    }

    enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kEnableOnStartup);
    int port = ini.ReadInt("General", "Port", kPort);
    if (port < kMinPort || port > kMaxPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinPort, kMaxPort);
        return {ReadStatus::Refused,
                "its [General] Port " + std::to_string(port) + " is outside " +
                    std::to_string(kMinPort) + "-" + std::to_string(kMaxPort) +
                    ", so the version that wrote this file did not start"};
    }
    udp_port = static_cast<uint16_t>(port);
    data_freshness_ms = ini.ReadInt("General", "DataFreshnessMs", kDataFreshnessMs);
    if (data_freshness_ms <= 0) {
        Log::Line("WARN: INI General.DataFreshnessMs %d is not a positive window; using %d",
                  data_freshness_ms, kDataFreshnessMs);
        data_freshness_ms = kDataFreshnessMs;
    }
    world_space_yaw = ini.ReadBool("General", "WorldSpaceYaw", kWorldSpaceYaw);
    show_aim_marker = ini.ReadBool("General", "ShowAimMarker", kShowAimMarker);

    fov_scale = ReadSanitized(ini, "FieldOfView", "Scale", kFovScale,
                              SanitizeFovScale);

    local_smoothing = ReadSanitized(
        ini, "Smoothing", "LocalSmoothing", kLocalSmoothing,
        [](float v) { return SanitizeSmoothing(v, kLocalSmoothing); });
    remote_smoothing = ReadSanitized(
        ini, "Smoothing", "RemoteSmoothing", kRemoteSmoothing,
        [](float v) { return SanitizeSmoothing(v, kRemoteSmoothing); });
    WarnRetiredSmoothingKey(ini, "Smoothing", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
    WarnRetiredShapingKeys(ini);
    WarnRetiredRecenterKeys(ini);

    position_enabled = ini.ReadBool("Position", "Enabled", kPositionEnabled);
    // Checked on the same terms as the rotation values above: these feed the position
    // processor and are then added straight to the view location, so one "LimitZ=nan"
    // puts a NaN in the camera's world position and the frame renders black.
    pos_limit_x = ReadPositionLimit(ini, "LimitX", kPosLimitX);
    pos_limit_y = ReadPositionLimit(ini, "LimitY", kPosLimitY);
    // Falls back to whatever LimitY resolved to, not to kPosLimitYDown: a config that
    // sets only LimitY would otherwise keep 0.20 m of downward travel while the upward
    // budget moved, with nothing in the log saying the key was half-effective.
    pos_limit_y_down = ReadPositionLimit(ini, "LimitYDown", pos_limit_y);
    pos_limit_z = ReadPositionLimit(ini, "LimitZ", kPosLimitZ);
    pos_limit_z_back = ReadPositionLimit(ini, "LimitZBack", kPosLimitZBack);
    // Scale keeps its sign and magnitude: it is the mod's main tuning knob and a
    // negative value is the same thing as inverting all three axes.
    position_scale = ReadSanitized(ini, "Position", "PositionScale", kPositionScale,
                                   [](float v) { return SanitizeFinite(v, kPositionScale); });
    collision_enabled = ini.ReadBool("Collision", "Enabled", kCollisionEnabled);
    collision_padding = ReadSanitized(
        ini, "Collision", "Padding", kCollisionPadding, [](float v) {
            return SanitizeCollisionPadding(v, kCollisionPadding, kMaxCollisionPadding);
        });

    vk_toggle     = ReadVirtualKey(ini, "Toggle",    kVkToggle);
    vk_cycle_mode = ReadVirtualKey(ini, "CycleMode", kVkCycleMode);
    vk_yaw_mode   = ReadVirtualKey(ini, "YawMode",   kVkYawMode);
    chord_toggle     = ini.ReadBool("Hotkeys", "ChordToggle",    kChord);
    chord_cycle_mode = ini.ReadBool("Hotkeys", "ChordCycleMode", kChord);
    chord_yaw_mode   = ini.ReadBool("Hotkeys", "ChordYawMode",   kChord);

    return {ReadStatus::Read, {}};
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"General", "EnableOnStartup"},
        {"General", "Port"},
        {"General", "DataFreshnessMs"},
        {"General", "WorldSpaceYaw"},
        {"General", "ShowAimMarker"},
        {"FieldOfView", "Scale"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Smoothing", "Smoothing"},
        {"Position", "Smoothing"},
        {"Sensitivity", "Yaw"},
        {"Sensitivity", "Pitch"},
        {"Sensitivity", "Roll"},
        {"Sensitivity", "InvertYaw"},
        {"Sensitivity", "InvertPitch"},
        {"Sensitivity", "InvertRoll"},
        {"Smoothing", "DeadzoneDeg"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Hotkeys", "Recenter"},
        {"Hotkeys", "ChordRecenter"},
        {"Position", "Enabled"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitYDown"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "PositionScale"},
        {"Collision", "Enabled"},
        {"Collision", "Padding"},
        {"Hotkeys", "Toggle"},
        {"Hotkeys", "CycleMode"},
        {"Hotkeys", "YawMode"},
        {"Hotkeys", "ChordToggle"},
        {"Hotkeys", "ChordCycleMode"},
        {"Hotkeys", "ChordYawMode"},
    };
}

}
