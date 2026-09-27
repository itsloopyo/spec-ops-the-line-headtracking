#include "config.h"

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

namespace SpecOpsTheLineHeadTracking {

namespace {

// The defaults live in config.h so the writer below, the reader's fallbacks and
// Config's own member initialisers all name the same constant.
using namespace defaults;

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// Returns false when the file could not be created, so the caller reports the real
// reason. Failing silently here surfaces one step later as "Failed to open INI",
// which reads as a corrupt file rather than a directory the game cannot write to.
bool WriteDefaultIni(const char* path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) {
        Log::Line("ERROR: could not create %s (error %lu). The mod cannot store its "
                  "settings; check that the game directory is writable.",
                  path, GetLastError());
        return false;
    }
    w.WriteComment(" Spec Ops: The Line - Head Tracking configuration");
    w.WriteComment(" Lives next to dinput8.dll in Binaries/Win32/.");
    w.WriteBlankLine();
    w.WriteSection("General");
    w.WriteBool("EnableOnStartup", kEnableOnStartup);
    w.WriteInt("Port", kPort);
    w.WriteInt("DataFreshnessMs", kDataFreshnessMs);
    w.WriteComment(" Yaw mode: true = horizon-locked yaw (default), false = camera-local.");
    w.WriteBool("WorldSpaceYaw", kWorldSpaceYaw);
    w.WriteComment(" Draw the mod's own marker at the aim point as well. The game's own");
    w.WriteComment(" crosshair is already moved there, so this is a diagnostic.");
    w.WriteBool("ShowAimMarker", kShowAimMarker);
    w.WriteBlankLine();
    w.WriteSection("FieldOfView");
    w.WriteComment(" Multiplies the field of view the game renders with. 1.0 leaves it alone;");
    w.WriteComment(" the game draws 72 degrees horizontally from the hip, so 1.25 gives 90.");
    w.WriteComment(" Range 0.5 - 2.0. Only the picture widens: the game keeps its own value");
    w.WriteComment(" for everything it decides with, so shots land exactly where they did.");
    w.WriteComment(" Aiming down sights is scaled by the same factor, so the sights keep");
    w.WriteComment(" their relative zoom.");
    w.WriteDouble("Scale", kFovScale);
    w.WriteBlankLine();
    w.WriteSection("Smoothing");
    w.WriteComment(" Smoothing 0.0 (responsive) - 1.0 (heavy). Covers rotation and position.");
    w.WriteComment(" The value is picked per connection from the packet source address:");
    w.WriteComment(" LocalSmoothing for a tracker running on this PC (loopback),");
    w.WriteComment(" RemoteSmoothing for a phone or other device on the network.");
    w.WriteDouble("LocalSmoothing", kLocalSmoothing);
    w.WriteDouble("RemoteSmoothing", kRemoteSmoothing);
    w.WriteBlankLine();
    w.WriteSection("Position");
    w.WriteComment(" 6DOF positional tracking. The pose is used at 1:1 - shape it in your tracker,");
    w.WriteComment(" not here. PositionScale = world units (cm) per metre of head translation.");
    w.WriteBool("Enabled", kPositionEnabled);
    w.WriteDouble("LimitX", kPosLimitX);
    w.WriteComment(" Vertical travel is clamped to [-LimitYDown, +LimitY]: how far the view");
    w.WriteComment(" may rise and how far it may drop, as separate metre budgets.");
    w.WriteDouble("LimitY", kPosLimitY);
    w.WriteDouble("LimitYDown", kPosLimitYDown);
    w.WriteDouble("LimitZ", kPosLimitZ);
    w.WriteDouble("LimitZBack", kPosLimitZBack);
    w.WriteDouble("PositionScale", kPositionScale);
    w.WriteBlankLine();
    w.WriteSection("Collision");
    w.WriteComment(" Stop a lean at the level's geometry instead of pushing the view through");
    w.WriteComment(" a wall. The game collides its own camera before head tracking is added,");
    w.WriteComment(" so without this a lean towards cover puts the camera inside it.");
    w.WriteBool("Enabled", kCollisionEnabled);
    w.WriteComment(" How far short of a surface the leaned view stops, in world units (cm).");
    w.WriteDouble("Padding", kCollisionPadding);
    w.WriteBlankLine();
    w.WriteSection("Hotkeys");
    w.WriteComment(" Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode).");
    w.WriteHex("Toggle", kVkToggle);
    w.WriteHex("CycleMode", kVkCycleMode);
    w.WriteHex("YawMode", kVkYawMode);
    w.WriteComment(" Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode).");
    w.WriteBool("ChordToggle", kChord);
    w.WriteBool("ChordCycleMode", kChord);
    w.WriteBool("ChordYawMode", kChord);
    w.Close();
    return true;
}

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

bool Config::LoadOrCreate(const char* iniPath) {
    if (!FileExists(iniPath) && !WriteDefaultIni(iniPath)) {
        return false;
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }

    enabled_on_startup = ini.ReadBool("General", "EnableOnStartup", kEnableOnStartup);
    int port = ini.ReadInt("General", "Port", kPort);
    if (port < kMinPort || port > kMaxPort) {
        Log::Line("ERROR: INI port %d out of range %d-%d", port, kMinPort, kMaxPort);
        return false;
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

    return true;
}

}
