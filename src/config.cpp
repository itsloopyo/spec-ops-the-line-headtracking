#include "config.h"

#include "legacy_config/legacy_config.h"
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

}

bool Config::LoadOrCreate(const char* iniPath) {
    if (!FileExists(iniPath) && !WriteDefaultIni(iniPath)) {
        return false;
    }

    // The reader is frozen in legacy_config/, so the file a player already has converts
    // exactly as this build reads it.
    legacy::Config c;
    const legacy::ReadResult read = c.Read(iniPath);
    if (read.status == legacy::ReadStatus::Absent) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }
    if (read.status == legacy::ReadStatus::Refused) {
        return false;
    }

    enabled_on_startup = c.enabled_on_startup;
    udp_port = c.udp_port;
    data_freshness_ms = c.data_freshness_ms;
    world_space_yaw = c.world_space_yaw;
    show_aim_marker = c.show_aim_marker;
    fov_scale = c.fov_scale;
    local_smoothing = c.local_smoothing;
    remote_smoothing = c.remote_smoothing;
    position_enabled = c.position_enabled;
    pos_limit_x = c.pos_limit_x;
    pos_limit_y = c.pos_limit_y;
    pos_limit_y_down = c.pos_limit_y_down;
    pos_limit_z = c.pos_limit_z;
    pos_limit_z_back = c.pos_limit_z_back;
    position_scale = c.position_scale;
    collision_enabled = c.collision_enabled;
    collision_padding = c.collision_padding;
    vk_toggle = c.vk_toggle;
    vk_cycle_mode = c.vk_cycle_mode;
    vk_yaw_mode = c.vk_yaw_mode;
    chord_toggle = c.chord_toggle;
    chord_cycle_mode = c.chord_cycle_mode;
    chord_yaw_mode = c.chord_yaw_mode;
    return true;
}

}
