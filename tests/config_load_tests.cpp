#include "test_support.h"

#include "config.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace SpecOpsTheLineHeadTracking;

namespace {

// IniReader reads through GetPrivateProfileString, which resolves a relative path
// against the Windows directory rather than the working directory and keeps a cache of
// the file it last read. So every case gets an absolute path of its own under TEMP.
std::string IniPath(const char* tag) {
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    return std::string(temp) + "spec_ops_headtracking_" + tag + ".ini";
}

void WriteIni(const std::string& path, const char* body) {
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    f << body;
}

// A missing file is written with the shipped defaults, and reading that file back gives
// exactly those defaults.
//
// This does NOT prove the writer and the reader agree on key NAMES: a key written as
// "LimitZback" and read as "LimitZBack" would fall through to the same default constant
// and every check below would still pass. WriterEmitsEveryKeyTests covers that.
int DefaultsRoundTripTests() {
    int failures = 0;
    const std::string path = IniPath("defaults");
    std::remove(path.c_str());

    Config created;
    CHECK(created.LoadOrCreate(path.c_str()));
    CHECK(std::ifstream(path.c_str()).good());

    Config cfg;
    CHECK(cfg.LoadOrCreate(path.c_str()));

    CHECK(cfg.enabled_on_startup == true);
    CHECK(cfg.udp_port == 4242);
    CHECK(cfg.data_freshness_ms == 500);
    CHECK(cfg.world_space_yaw == true);
    CHECK(cfg.show_aim_marker == false);
    CHECK_NEAR(cfg.fov_scale, 1.0, 1e-6);
    CHECK_NEAR(cfg.local_smoothing, 0.0, 1e-6);
    CHECK_NEAR(cfg.remote_smoothing, 0.15, 1e-6);
    CHECK(cfg.position_enabled == true);
    CHECK_NEAR(cfg.pos_limit_x, 0.30, 1e-6);
    CHECK_NEAR(cfg.pos_limit_y, 0.20, 1e-6);
    CHECK_NEAR(cfg.pos_limit_y_down, 0.20, 1e-6);
    CHECK_NEAR(cfg.pos_limit_z, 0.40, 1e-6);
    CHECK_NEAR(cfg.pos_limit_z_back, 0.10, 1e-6);
    CHECK_NEAR(cfg.position_scale, 100.0, 1e-6);
    CHECK(cfg.collision_enabled == true);
    CHECK_NEAR(cfg.collision_padding, 10.0, 1e-6);
    CHECK(cfg.vk_toggle == 0x23);
    CHECK(cfg.vk_cycle_mode == 0x21);
    CHECK(cfg.vk_yaw_mode == 0x22);
    CHECK(cfg.chord_toggle == true);

    std::remove(path.c_str());
    return failures;
}

// Reads the file WriteDefaultIni actually produced and asserts each section and key name
// is literally in it.
//
// This is the half that pins the WRITER. Neither of the other two config tests can:
// DefaultsRoundTripTests reads a key the writer misnamed, falls through to the same
// default constant it then asserts, and passes; ReaderKeyNamesTests hand-writes its own
// fixture, so it pins the reader against the test and never touches the writer. Rename a
// key in WriteDefaultIni alone and only this test fails.
int WriterEmitsEveryKeyTests() {
    int failures = 0;
    const std::string path = IniPath("writerkeys");
    std::remove(path.c_str());

    Config created;
    CHECK(created.LoadOrCreate(path.c_str()));

    std::ifstream file(path.c_str(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    file.close();
    CHECK(!text.empty());

    static const char* kExpected[] = {
        "[General]", "EnableOnStartup=", "Port=", "DataFreshnessMs=", "WorldSpaceYaw=",
        "ShowAimMarker=",
        "[FieldOfView]", "Scale=",
        "[Smoothing]", "LocalSmoothing=", "RemoteSmoothing=",
        "[Position]", "Enabled=", "LimitX=", "LimitY=", "LimitYDown=", "LimitZ=",
        "LimitZBack=", "PositionScale=",
        "[Collision]", "Padding=",
        "[Hotkeys]", "Toggle=", "CycleMode=", "YawMode=", "ChordToggle=",
        "ChordCycleMode=", "ChordYawMode=",
    };
    for (const char* key : kExpected) {
        const bool present = text.find(key) != std::string::npos;
        failures += ReportCheck(present, key, __FILE__, __LINE__);
    }

    // "LimitY=" is a prefix of "LimitYDown=", so the check above cannot tell a file that
    // has both from one that has only the longer key. Pin the shorter one on its own.
    CHECK(text.find("\nLimitY=") != std::string::npos ||
          text.find("\rLimitY=") != std::string::npos);

    // Names alone do not pin which SECTION a key landed in, and every reader looks its
    // key up under one specific section. PositionScale drifting into [Hotkeys] keeps its
    // name, so every check above still passes, while the reader finds nothing and falls
    // back to the default.
    static const struct { const char* section; const char* key; } kPlacement[] = {
        { "[General]", "EnableOnStartup=" }, { "[General]", "ShowAimMarker=" },
        { "[FieldOfView]", "Scale=" },
        { "[Smoothing]", "LocalSmoothing=" }, { "[Smoothing]", "RemoteSmoothing=" },
        { "[Position]", "Enabled=" },        { "[Position]", "LimitYDown=" },
        { "[Position]", "PositionScale=" },
        { "[Collision]", "Enabled=" },       { "[Collision]", "Padding=" },
        { "[Hotkeys]", "Toggle=" },          { "[Hotkeys]", "ChordYawMode=" },
    };
    static const char* kAllSections[] = { "[General]", "[FieldOfView]", "[Smoothing]",
                                          "[Position]", "[Collision]", "[Hotkeys]" };
    for (const auto& entry : kPlacement) {
        const size_t section = text.find(entry.section);
        // Searched from the section header rather than from the start of the file:
        // [Position] and [Collision] both have an Enabled key, and a global search would
        // answer both placement checks with the first one.
        const size_t key = text.find(entry.key, section);
        bool placed = section != std::string::npos && key != std::string::npos &&
                      key > section;
        if (placed) {
            // No other section header may sit between the header and the key.
            for (const char* header : kAllSections) {
                const size_t other = text.find(header);
                if (other != std::string::npos && other > section && other < key) {
                    placed = false;
                    break;
                }
            }
        }
        failures += ReportCheck(placed, entry.key, __FILE__, __LINE__);
    }

    std::remove(path.c_str());
    return failures;
}

// Every key the reader consults, given a NON-default value in a hand-written file. This
// pins the READER's names; WriterEmitsEveryKeyTests above pins the writer's. Both are
// needed - a name that drifts in one place only fails one of them.
int ReaderKeyNamesTests() {
    int failures = 0;
    const std::string path = IniPath("keynames");
    WriteIni(path,
             "[General]\r\n"
             "EnableOnStartup=false\r\n"
             "Port=5555\r\n"
             "DataFreshnessMs=250\r\n"
             "WorldSpaceYaw=false\r\n"
             "ShowAimMarker=true\r\n"
             "[FieldOfView]\r\n"
             "Scale=1.75\r\n"
             "[Smoothing]\r\n"
             "LocalSmoothing=0.25\r\n"
             "RemoteSmoothing=0.75\r\n"
             "[Position]\r\n"
             "Enabled=false\r\n"
             "LimitX=0.11\r\n"
             "LimitY=0.12\r\n"
             "LimitYDown=0.13\r\n"
             "LimitZ=0.14\r\n"
             "LimitZBack=0.15\r\n"
             "PositionScale=42.5\r\n"
             "[Collision]\r\n"
             "Enabled=false\r\n"
             "Padding=17.5\r\n"
             "[Hotkeys]\r\n"
             "Toggle=0x41\r\n"
             "CycleMode=0x42\r\n"
             "YawMode=0x43\r\n"
             "ChordToggle=false\r\n"
             "ChordCycleMode=false\r\n"
             "ChordYawMode=false\r\n");

    Config cfg;
    CHECK(cfg.LoadOrCreate(path.c_str()));

    CHECK(cfg.enabled_on_startup == false);
    CHECK(cfg.udp_port == 5555);
    CHECK(cfg.data_freshness_ms == 250);
    CHECK(cfg.world_space_yaw == false);
    CHECK(cfg.show_aim_marker == true);
    CHECK_NEAR(cfg.fov_scale, 1.75, 1e-6);
    CHECK_NEAR(cfg.local_smoothing, 0.25, 1e-6);
    CHECK_NEAR(cfg.remote_smoothing, 0.75, 1e-6);
    CHECK(cfg.position_enabled == false);
    CHECK_NEAR(cfg.pos_limit_x, 0.11, 1e-6);
    CHECK_NEAR(cfg.pos_limit_y, 0.12, 1e-6);
    CHECK_NEAR(cfg.pos_limit_y_down, 0.13, 1e-6);
    CHECK_NEAR(cfg.pos_limit_z, 0.14, 1e-6);
    CHECK_NEAR(cfg.pos_limit_z_back, 0.15, 1e-6);
    CHECK_NEAR(cfg.position_scale, 42.5, 1e-6);
    CHECK(cfg.collision_enabled == false);
    CHECK_NEAR(cfg.collision_padding, 17.5, 1e-6);
    CHECK(cfg.vk_toggle == 0x41);
    CHECK(cfg.vk_cycle_mode == 0x42);
    CHECK(cfg.vk_yaw_mode == 0x43);
    CHECK(cfg.chord_toggle == false);
    CHECK(cfg.chord_cycle_mode == false);
    CHECK(cfg.chord_yaw_mode == false);

    std::remove(path.c_str());
    return failures;
}

// The two vertical limits are independent budgets. LimitY used to be the only one that
// reached the processor, leaving the downward clamp on the core struct's own 0.20
// whatever the user asked for.
int VerticalLimitsAreIndependentTests() {
    int failures = 0;
    const std::string path = IniPath("limity");
    WriteIni(path,
             "[Position]\r\n"
             "LimitY=0.45\r\n"
             "LimitYDown=0.05\r\n");

    Config cfg;
    CHECK(cfg.LoadOrCreate(path.c_str()));
    CHECK_NEAR(cfg.pos_limit_y, 0.45, 1e-6);
    CHECK_NEAR(cfg.pos_limit_y_down, 0.05, 1e-6);

    // A negative limit would hand the processor lo > hi, pinning the offset at a
    // constant instead of bounding it.
    WriteIni(path,
             "[Position]\r\n"
             "LimitYDown=-0.3\r\n");
    Config negative;
    CHECK(negative.LoadOrCreate(path.c_str()));
    CHECK_NEAR(negative.pos_limit_y_down, 0.0, 1e-6);

    // An INI written before LimitYDown existed carries LimitY alone, and has to give
    // symmetric travel. Falling back to kPosLimitYDown instead left a player who set
    // LimitY=0.45 with 0.45 m up and 0.20 m down.
    WriteIni(path,
             "[Position]\r\n"
             "LimitY=0.45\r\n");
    Config mirrored;
    CHECK(mirrored.LoadOrCreate(path.c_str()));
    CHECK_NEAR(mirrored.pos_limit_y, 0.45, 1e-6);
    CHECK_NEAR(mirrored.pos_limit_y_down, 0.45, 1e-6);

    WriteIni(path,
             "[Position]\r\n"
             "LimitY=0.05\r\n");
    Config tightened;
    CHECK(tightened.LoadOrCreate(path.c_str()));
    CHECK_NEAR(tightened.pos_limit_y, 0.05, 1e-6);
    CHECK_NEAR(tightened.pos_limit_y_down, 0.05, 1e-6);

    std::remove(path.c_str());
    return failures;
}

// The retired pose-shaping keys are IGNORED, not honoured and not an error. An INI from
// an older build still carries them, and the mod has to load normally while they sit
// there doing nothing - the log line is what tells the user to move them into the
// tracker. Reading one back into a struct field is impossible now (the fields are gone),
// so what this pins is that their presence does not break the load or disturb the keys
// around them.
int RetiredShapingKeysAreIgnoredTests() {
    int failures = 0;
    const std::string path = IniPath("retired");
    WriteIni(path,
             "Yaw=2.5\r\n"
             "Pitch=2.5\r\n"
             "Roll=2.5\r\n"
             "[Smoothing]\r\n"
             "LocalSmoothing=0.3\r\n"
             "DeadzoneDeg=1.5\r\n"
             "[Position]\r\n"
             "SensitivityX=9.0\r\n"
             "LimitY=0.33\r\n");

    Config cfg;
    CHECK(cfg.LoadOrCreate(path.c_str()));
    // The live keys in the same sections are still read correctly.
    CHECK_NEAR(cfg.local_smoothing, 0.3, 1e-6);
    CHECK_NEAR(cfg.pos_limit_y, 0.33, 1e-6);
    // And the defaults elsewhere are untouched by the retired keys' presence.
    CHECK_NEAR(cfg.remote_smoothing, 0.15, 1e-6);
    CHECK(cfg.udp_port == 4242);

    std::remove(path.c_str());
    return failures;
}

int ValueReadTests() {
    int failures = 0;
    const std::string path = IniPath("values");
    WriteIni(path,
             "[General]\r\n"
             "EnableOnStartup=false\r\n"
             "Port=5000\r\n"
             "WorldSpaceYaw=false\r\n"
             "ShowAimMarker=true\r\n"
             "[FieldOfView]\r\n"
             "Scale=1.25\r\n"
             "Yaw=2.5\r\n"
             "[Smoothing]\r\n"
             "LocalSmoothing=0.4\r\n"
             "RemoteSmoothing=0.6\r\n"
             "DeadzoneDeg=1.5\r\n"
             "[Hotkeys]\r\n"
             "Toggle=0x24\r\n");

    Config cfg;
    CHECK(cfg.LoadOrCreate(path.c_str()));
    CHECK(cfg.enabled_on_startup == false);
    CHECK(cfg.udp_port == 5000);
    CHECK(cfg.world_space_yaw == false);
    CHECK(cfg.show_aim_marker == true);
    CHECK_NEAR(cfg.fov_scale, 1.25, 1e-6);
    CHECK_NEAR(cfg.local_smoothing, 0.4, 1e-6);
    CHECK_NEAR(cfg.remote_smoothing, 0.6, 1e-6);
    CHECK(cfg.vk_toggle == 0x24);
    // Keys the file leaves out keep their shipped defaults.
    CHECK(cfg.data_freshness_ms == 500);

    std::remove(path.c_str());
    return failures;
}

// Boundary validation on the values that feed the smoothing and view-matrix maths.
int SanitizationTests() {
    int failures = 0;
    const std::string path = IniPath("sanitize");
    WriteIni(path,
             "[FieldOfView]\r\n"
             "Scale=9.0\r\n"
             "Yaw=nan\r\n"
             "[Smoothing]\r\n"
             "LocalSmoothing=5.0\r\n"
             "RemoteSmoothing=nan\r\n"
             "DeadzoneDeg=-3.0\r\n");

    Config cfg;
    CHECK(cfg.LoadOrCreate(path.c_str()));
    CHECK_NEAR(cfg.fov_scale, 2.0, 1e-6);
    CHECK_NEAR(cfg.local_smoothing, 1.0, 1e-6);
    CHECK_NEAR(cfg.remote_smoothing, 0.15, 1e-6);

    std::remove(path.c_str());
    return failures;
}

// An out-of-range port is refused outright rather than corrected, because binding a port
// the user did not ask for is worse than not binding at all.
int PortRangeTests() {
    int failures = 0;

    const std::string low = IniPath("port_low");
    WriteIni(low, "[General]\r\nPort=80\r\n");
    Config lowCfg;
    CHECK(!lowCfg.LoadOrCreate(low.c_str()));
    std::remove(low.c_str());

    const std::string high = IniPath("port_high");
    WriteIni(high, "[General]\r\nPort=70000\r\n");
    Config highCfg;
    CHECK(!highCfg.LoadOrCreate(high.c_str()));
    std::remove(high.c_str());

    const std::string edge = IniPath("port_edge");
    WriteIni(edge, "[General]\r\nPort=1024\r\n");
    Config edgeCfg;
    CHECK(edgeCfg.LoadOrCreate(edge.c_str()));
    CHECK(edgeCfg.udp_port == 1024);
    std::remove(edge.c_str());

    return failures;
}

// The [Position] floats get the same finite check as the rotation ones: they feed the
// position processor and are then added to the camera's world location, so a NaN here
// puts a NaN in the camera position and renders a black frame. Magnitude is left alone -
// the README quotes no ceiling on a limit, and PositionScale is the mod's main tuning
// knob - but a negative limit is refused, because ClampToLimits would then get lo > hi
// and pin the offset at a constant instead of bounding it.
int PositionValuesAreCheckedTests() {
    int failures = 0;
    const std::string path = IniPath("position");
    WriteIni(path,
             "[Position]\r\n"
             "SensitivityX=nan\r\n"
             "LimitX=-0.5\r\n"
             "LimitY=nan\r\n"
             "LimitZ=99.0\r\n"
             "PositionScale=inf\r\n");

    Config cfg;
    CHECK(cfg.LoadOrCreate(path.c_str()));
    CHECK_NEAR(cfg.pos_limit_x, 0.0, 1e-6);
    CHECK_NEAR(cfg.pos_limit_y, 0.20, 1e-6);
    CHECK_NEAR(cfg.pos_limit_y_down, 0.20, 1e-6);
    CHECK_NEAR(cfg.pos_limit_z, 99.0, 1e-6);
    CHECK_NEAR(cfg.position_scale, 100.0, 1e-6);

    std::remove(path.c_str());
    return failures;
}

// A window of zero or less would hold no pose at all and kill tracking silently, so it
// falls back to the shipped default rather than being taken at face value.
int DataFreshnessTests() {
    int failures = 0;
    const std::string path = IniPath("freshness");
    WriteIni(path, "[General]\r\nDataFreshnessMs=0\r\n");

    Config cfg;
    CHECK(cfg.LoadOrCreate(path.c_str()));
    CHECK(cfg.data_freshness_ms == 500);

    std::remove(path.c_str());
    return failures;
}

// A virtual-key code outside GetAsyncKeyState's 0x01-0xFE range polls nothing, so the
// hotkey would silently never fire. It falls back to the shipped binding rather than
// shipping a dead key; 0 is the poller's own "unbound" sentinel and is left alone.
int HotkeyRangeTests() {
    int failures = 0;

    const std::string bad = IniPath("hotkeys_bad");
    WriteIni(bad,
             "[Hotkeys]\r\n"
             "Toggle=0x2300\r\n"
             "CycleMode=0xFF\r\n"
             "YawMode=0x7A\r\n");
    Config cfg;
    CHECK(cfg.LoadOrCreate(bad.c_str()));
    CHECK(cfg.vk_toggle == 0x23);
    CHECK(cfg.vk_cycle_mode == 0x21);
    // In range, so the user's own choice survives untouched.
    CHECK(cfg.vk_yaw_mode == 0x7A);
    std::remove(bad.c_str());

    // 0 unbinds a hotkey in the poller, so it is a legitimate value, not a typo.
    const std::string unbound = IniPath("hotkeys_unbound");
    WriteIni(unbound, "[Hotkeys]\r\nToggle=0x0\r\n");
    Config unboundCfg;
    CHECK(unboundCfg.LoadOrCreate(unbound.c_str()));
    CHECK(unboundCfg.vk_toggle == 0);
    std::remove(unbound.c_str());

    // The boundary itself is accepted.
    const std::string edge = IniPath("hotkeys_edge");
    WriteIni(edge, "[Hotkeys]\r\nToggle=0xFE\r\nCycleMode=0x1\r\n");
    Config edgeCfg;
    CHECK(edgeCfg.LoadOrCreate(edge.c_str()));
    CHECK(edgeCfg.vk_toggle == 0xFE);
    CHECK(edgeCfg.vk_cycle_mode == 0x1);
    std::remove(edge.c_str());

    return failures;
}

}  // namespace

int RunConfigLoadTests() {
    std::printf("Config load\n");
    int failures = DefaultsRoundTripTests();
    failures += WriterEmitsEveryKeyTests();
    failures += ReaderKeyNamesTests();
    failures += VerticalLimitsAreIndependentTests();
    failures += RetiredShapingKeysAreIgnoredTests();
    failures += ValueReadTests();
    failures += SanitizationTests();
    failures += PortRangeTests();
    failures += PositionValuesAreCheckedTests();
    failures += DataFreshnessTests();
    failures += HotkeyRangeTests();
    return failures;
}
