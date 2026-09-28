// The config differential test (convert-a-mod-to-the-canonical-config, section 5). Every input
// is read three ways:
//
//   oracle     the reader of the newest published build, the rolling `dev` pre-release at
//              3e9a526, with the core sources it compiled at its pin 660b1f8 (oracle_adapter.h)
//   import     the frozen reader in src/legacy_config/
//   migration  the config owner in a folder holding only SpecOpsTheLineHeadTracking.ini, the legacy
//              file, importing it into a new CameraUnlock.ini, then the canonical reader and
//              table on that file
//
// Comparison 1, oracle against import, on every input: load status, every field both read
// (floats bit for bit), the startup state, and which actions every key press fires under every
// set of held modifiers. dev's reader is the one the conversion froze, so it finds nothing.
//
// Comparison 2, import against migration, is the proof for the migration: the settings the mod
// starts on and the actions every key press fires are the import's, apart from the approved
// changes, each of which the import must record as dropped. A PositionScale the player set away
// from its shipped 100 is dropped (pose_shaping), ShowAimMarker=true is dropped (reticle: the
// marker is a diagnostic with no setting), a hotkey code on Ctrl, Shift or Alt alone is unbound
// with its chord kept (N3), and a [Position] limit above the rows' 10 m, which the published
// build read with no upper bound, imports as 10 (N4).
//
// A setting the player never changed from what the published build shipped follows Defaults.ini:
// the import lists its row in follows_defaults_ini, the tracking mode pair as one unit, and the
// migration writes it `default`. The test derives that list from what the frozen reader read and
// holds the import's list to it on every input; the dev build's first-run output, an empty file
// and no file leave every row to Defaults.ini.
//
// Comparison 2 runs twice, once over a Defaults.ini at the built-in values and once over one a
// player changed on every row this game takes from it. Over the changed one, a row left to
// Defaults.ini runs on that file's value and is written `default`, and every other row runs on
// the import's. After every load SpecOpsTheLineHeadTracking.ini keeps its bytes, its write time and
// its attributes, Defaults.ini is never written, and the folder holds the legacy file and
// CameraUnlock.ini and nothing else (the legacy file alone when nothing was imported). The next
// load reads CameraUnlock.ini, imports nothing and writes nothing, and a read-only legacy file
// imports as a writable one does.
//
// The distinct migrated files are written beside the executable under migrated\, for
// lint-migrated.mjs to run core's canonical config lint over.
//
// The key presses go through the published build's own Hotkeys::Start and the guards it
// compiled (OracleFires), and through the guard the current build registers (CurrentFires).
//
// Inputs: no file, an empty file, the dev build's first-run output (no build shipped a config
// or a launcher seed), and core's corpus over that output.
//
// `--write-first-run <path>` writes the oracle's first-run output to <path> and exits; that is
// how data/dev-first-run.ini was extracted.

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace SpecOpsTheLineHeadTracking;

namespace {

// The published build read the five [Position] limits with no upper bound, and the canonical
// rows take 0 to 10 metres, so a limit above 10 imports as 10 (N4).
constexpr float kMaxCanonicalLimit = 10.0f;

float ClampedLimit(float read) { return read > kMaxCanonicalLimit ? kMaxCanonicalLimit : read; }

constexpr const char* kFileName = "SpecOpsTheLineHeadTracking.ini";

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        if (g_failures < 200) std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

struct Listing {
    std::vector<std::pair<std::string, std::string>> files;
    bool operator==(const Listing& o) const { return files == o.files; }
};

Listing List(const fs::path& dir) {
    Listing l;
    for (const auto& e : fs::directory_iterator(dir)) {
        l.files.emplace_back(e.path().filename().string(), ReadBytes(e.path()));
    }
    std::sort(l.files.begin(), l.files.end());
    return l;
}

void SetReadOnly(const fs::path& path, bool readOnly) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("no attributes for " + path.string());
    const DWORD next = readOnly ? (attrs | FILE_ATTRIBUTE_READONLY) : (attrs & ~FILE_ATTRIBUTE_READONLY);
    if (!SetFileAttributesW(path.c_str(), next)) throw std::runtime_error("could not set attributes on " + path.string());
}

struct Input {
    std::string name;
    std::optional<std::string> bytes;  // nullopt: no file
};

// Startup state as dev:src/tracking_runtime.cpp Start derives it from the config: enabled
// from enabled_on_startup, RotationAndPosition when position_enabled else RotationOnly, the
// yaw mode from world_space_yaw.
struct Startup {
    bool enabled;
    int mode;  // 0 rotation and position, 1 rotation only
    bool world_space_yaw;
};

template <class C>
Startup StartupOf(const C& c) {
    return {c.enabled_on_startup, c.position_enabled ? 0 : 1, c.world_space_yaw};
}

bool SameStartup(const Startup& a, const Startup& b) {
    return a.enabled == b.enabled && a.mode == b.mode && a.world_space_yaw == b.world_space_yaw;
}

template <class C>
specops_oracle_view::HotkeyView KeysOf(const C& c) {
    return {c.vk_toggle, c.vk_cycle_mode, c.vk_yaw_mode, c.chord_toggle, c.chord_cycle_mode, c.chord_yaw_mode};
}

cameraunlock::input::KeyModifiers g_currentHeld = cameraunlock::input::KeyModifiers::kNone;

cameraunlock::input::KeyModifiers CurrentHeld() { return g_currentHeld; }

cameraunlock::input::KeyModifiers ModifiersOf(int held) {
    using cameraunlock::input::KeyModifiers;
    KeyModifiers m = KeyModifiers::kNone;
    if ((held & 1) != 0) m = m | KeyModifiers::kCtrl;
    if ((held & 2) != 0) m = m | KeyModifiers::kShift;
    if ((held & 4) != 0) m = m | KeyModifiers::kAlt;
    return m;
}

// OracleFires' table for the current build. Its Hotkeys::Start parses each key list and hands
// it to RegisterKeyBindings, which puts one detail::GuardKey callback per distinct key on the
// poller, holding that key's bindings in list order. The same callbacks are built here with
// the held modifiers read from the test rather than the keyboard, since the poller keeps its
// callbacks to itself.
specops_oracle_view::FireTable CurrentFires(const Config& m) {
    using specops_oracle_view::kFirstKey;
    using specops_oracle_view::kHeldStates;
    using specops_oracle_view::kLastKey;
    std::array<int, 3> fired{};
    std::vector<std::pair<int, std::function<void()>>> registered;
    const std::string* lists[3] = {&m.toggle_key_name, &m.cycle_tracking_mode_key_name, &m.yaw_mode_key_name};
    for (int action = 0; action < 3; ++action) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*lists[action]);
        if (!parsed.ok()) throw std::logic_error("migrated hotkey list '" + *lists[action] + "' does not parse");
        std::vector<int> keys;
        std::vector<std::vector<cameraunlock::input::KeyModifiers>> modifiers;
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            const auto at = std::find(keys.begin(), keys.end(), b.vk);
            if (at == keys.end()) {
                keys.push_back(b.vk);
                modifiers.push_back({b.modifiers});
            } else {
                modifiers[static_cast<std::size_t>(at - keys.begin())].push_back(b.modifiers);
            }
        }
        for (std::size_t i = 0; i < keys.size(); ++i) {
            registered.emplace_back(keys[i], cameraunlock::input::detail::GuardKey(
                                                 std::move(modifiers[i]), [&fired, action] { ++fired[action]; },
                                                 &CurrentHeld));
        }
    }

    specops_oracle_view::FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            g_currentHeld = ModifiersOf(held);
            for (const auto& r : registered) {
                if (r.first == vk) r.second();
            }
            table.push_back(fired);
        }
    }
    g_currentHeld = cameraunlock::input::KeyModifiers::kNone;
    return table;
}

// The first press whose fired actions differ, for the failure message.
std::string FirstFireDifference(const specops_oracle_view::FireTable& expected, const specops_oracle_view::FireTable& got) {
    for (std::size_t i = 0; i < expected.size() && i < got.size(); ++i) {
        if (expected[i] != got[i]) {
            char text[160];
            std::snprintf(text, sizeof text, "key 0x%02X held %d fires %d/%d/%d, not %d/%d/%d",
                          static_cast<int>(i / specops_oracle_view::kHeldStates) + specops_oracle_view::kFirstKey,
                          static_cast<int>(i % specops_oracle_view::kHeldStates), got[i][0], got[i][1], got[i][2],
                          expected[i][0], expected[i][1], expected[i][2]);
            return text;
        }
    }
    return expected.size() == got.size() ? "none" : "the tables differ in size";
}

// Every field the import reads, against the oracle's field of the same name.
std::vector<std::string> FieldDifferences(const specops_oracle_view::OracleConfig& o, const legacy::Config& i) {
    std::vector<std::string> d;
    auto b = [&d](const char* n, bool x, bool y) { if (x != y) d.push_back(n); };
    auto f = [&d](const char* n, float x, float y) { if (!SameBits(x, y)) d.push_back(n); };
    auto n = [&d](const char* name, long long x, long long y) { if (x != y) d.push_back(name); };
    b("enabled_on_startup", o.enabled_on_startup, i.enabled_on_startup);
    n("udp_port", o.udp_port, i.udp_port);
    f("local_smoothing", o.local_smoothing, i.local_smoothing);
    f("remote_smoothing", o.remote_smoothing, i.remote_smoothing);
    b("show_aim_marker", o.show_aim_marker, i.show_aim_marker);
    n("data_freshness_ms", o.data_freshness_ms, i.data_freshness_ms);
    b("world_space_yaw", o.world_space_yaw, i.world_space_yaw);
    f("fov_scale", o.fov_scale, i.fov_scale);
    b("position_enabled", o.position_enabled, i.position_enabled);
    f("pos_limit_x", o.pos_limit_x, i.pos_limit_x);
    f("pos_limit_y", o.pos_limit_y, i.pos_limit_y);
    f("pos_limit_y_down", o.pos_limit_y_down, i.pos_limit_y_down);
    f("pos_limit_z", o.pos_limit_z, i.pos_limit_z);
    f("pos_limit_z_back", o.pos_limit_z_back, i.pos_limit_z_back);
    f("position_scale", o.position_scale, i.position_scale);
    b("collision_enabled", o.collision_enabled, i.collision_enabled);
    f("collision_padding", o.collision_padding, i.collision_padding);
    n("vk_toggle", o.vk_toggle, i.vk_toggle);
    n("vk_cycle_mode", o.vk_cycle_mode, i.vk_cycle_mode);
    n("vk_yaw_mode", o.vk_yaw_mode, i.vk_yaw_mode);
    b("chord_toggle", o.chord_toggle, i.chord_toggle);
    b("chord_cycle_mode", o.chord_cycle_mode, i.chord_cycle_mode);
    b("chord_yaw_mode", o.chord_yaw_mode, i.chord_yaw_mode);
    return d;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

// The corpus descriptor of every key the frozen reader reads.
std::vector<cameraunlock::config::testing::MutationKey> MutationKeys() {
    using cameraunlock::config::testing::ChordSwitch;
    using cameraunlock::config::testing::MutationKey;
    auto plain = [](const char* s, const char* k, const char* alt, std::vector<std::string> oor = {}) {
        MutationKey m;
        m.section = s;
        m.key = k;
        m.alternate = alt;
        m.out_of_range = std::move(oor);
        return m;
    };
    auto hotkey = [](const char* k, const char* alt, const char* chord) {
        MutationKey m;
        m.section = "Hotkeys";
        m.key = k;
        m.alternate = alt;
        m.out_of_range = {"0x100"};
        m.hotkey = true;
        m.chords.push_back(ChordSwitch{"Hotkeys", chord, "true", "false"});
        return m;
    };
    return {
        plain("General", "EnableOnStartup", "false"),
        plain("General", "Port", "4243", {"1023", "65536"}),
        plain("General", "DataFreshnessMs", "250", {"0"}),
        plain("General", "WorldSpaceYaw", "false"),
        plain("General", "ShowAimMarker", "true"),
        plain("FieldOfView", "Scale", "1.25", {"0.4", "2.1"}),
        plain("Smoothing", "LocalSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Smoothing", "RemoteSmoothing", "0.3", {"-0.5", "1.5"}),
        plain("Smoothing", "Smoothing", "0.3"),
        plain("Position", "Smoothing", "0.3"),
        plain("Sensitivity", "Yaw", "0.5"),
        plain("Sensitivity", "Pitch", "0.5"),
        plain("Sensitivity", "Roll", "0.5"),
        plain("Sensitivity", "InvertYaw", "true"),
        plain("Sensitivity", "InvertPitch", "true"),
        plain("Sensitivity", "InvertRoll", "true"),
        plain("Smoothing", "DeadzoneDeg", "1.0"),
        plain("Position", "SensitivityX", "0.5"),
        plain("Position", "SensitivityY", "0.5"),
        plain("Position", "SensitivityZ", "0.5"),
        plain("Position", "InvertX", "true"),
        plain("Position", "InvertY", "true"),
        plain("Position", "InvertZ", "true"),
        plain("Hotkeys", "Recenter", "0x24"),
        plain("Hotkeys", "ChordRecenter", "true"),
        plain("Position", "Enabled", "false"),
        plain("Position", "LimitX", "0.5", {"-0.3"}),
        plain("Position", "LimitY", "0.5", {"-0.2"}),
        plain("Position", "LimitYDown", "0.3", {"-0.2"}),
        plain("Position", "LimitZ", "0.5", {"-0.4"}),
        plain("Position", "LimitZBack", "0.2", {"-0.1"}),
        plain("Position", "PositionScale", "50"),
        plain("Collision", "Enabled", "false"),
        plain("Collision", "Padding", "20", {"-1", "101"}),
        hotkey("Toggle", "0x70", "ChordToggle"),
        hotkey("CycleMode", "0x71", "ChordCycleMode"),
        hotkey("YawMode", "0x72", "ChordYawMode"),
        plain("Hotkeys", "ChordToggle", "false"),
        plain("Hotkeys", "ChordCycleMode", "false"),
        plain("Hotkeys", "ChordYawMode", "false"),
    };
}

// One folder per reading under a root of this process's own, emptied before each input so the
// test never holds more than one input's files.
class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("specops-config-differential-" + std::to_string(GetCurrentProcessId()));
        Remove(root_);
        fs::create_directories(root_);
    }
    ~Scratch() { Remove(root_); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    // root/leaf, created empty.
    fs::path Clean(const std::string& leaf) {
        const fs::path dir = root_ / leaf;
        Remove(dir);
        fs::create_directories(dir);
        return dir;
    }
    const fs::path& Root() const { return root_; }

private:
    // Read-only files included, which remove_all will not delete.
    static void Remove(const fs::path& dir) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (e.is_regular_file()) SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(dir, ec);
        if (ec) throw std::runtime_error("could not empty " + dir.string() + ": " + ec.message());
    }

    fs::path root_;
};

fs::path Place(const fs::path& dir, const Input& input) {
    const fs::path file = dir / kFileName;
    if (input.bytes) WriteBytes(file, *input.bytes);
    return file;
}

struct ImportRun {
    legacy::Config config;
    legacy::ReadResult result;
};

// The import on a read-only copy of the input, which must leave its folder as it found it.
ImportRun RunImport(Scratch& scratch, const Input& input) {
    const fs::path dir = scratch.Clean("import");
    const fs::path file = Place(dir, input);
    if (input.bytes) SetReadOnly(file, true);
    const Listing before = List(dir);
    ImportRun run;
    run.result = run.config.Read(file.string().c_str());
    Check(List(dir) == before, input.name + ": the import changed its folder");
    return run;
}

ImportRun Comparison1(Scratch& scratch, const Input& input) {
    const fs::path odir = scratch.Clean("oracle");
    const specops_oracle_view::OracleResult oracle = specops_oracle_view::RunOracle(Place(odir, input).string());
    const ImportRun import = RunImport(scratch, input);

    const bool importUsable = import.result.status != legacy::ReadStatus::Refused;
    Check(oracle.loaded == importUsable, input.name + ": load status differs (oracle " +
                                             (oracle.loaded ? "loaded" : "refused") + ")");
    Check(input.bytes.has_value() || import.result.status == legacy::ReadStatus::Absent,
          input.name + ": no file is not Absent");
    if (oracle.loaded && importUsable) {
        const std::vector<std::string> fields = FieldDifferences(oracle.config, import.config);
        Check(fields.empty(), input.name + ": fields differ: " + Join(fields));
        Check(SameStartup(StartupOf(oracle.config), StartupOf(import.config)), input.name + ": startup state differs");
        const specops_oracle_view::FireTable oracleFires = specops_oracle_view::OracleFires(KeysOf(oracle.config));
        const specops_oracle_view::FireTable importFires = specops_oracle_view::OracleFires(KeysOf(import.config));
        Check(oracleFires == importFires,
              input.name + ": hotkeys fire differently: " + FirstFireDifference(oracleFires, importFires));
    }
    return import;
}

// ---------------------------------------------------------------------------
// Comparison 2
// ---------------------------------------------------------------------------

namespace cfg = cameraunlock::config;
using cfg::ConfigLoadStatus;
using cfg::DropRule;
using cfg::DroppedValue;
using cfg::ImportResult;
using cfg::ImportStatus;

// A file as the test holds it to: its bytes, its last write time and its attributes.
struct FileStamp {
    std::string bytes;
    FILETIME written{};
    DWORD attributes = 0;

    bool operator==(const FileStamp& o) const {
        return bytes == o.bytes && CompareFileTime(&written, &o.written) == 0 && attributes == o.attributes;
    }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};

FileStamp Stamp(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        throw std::runtime_error("cannot stat " + path.string());
    }
    FileStamp s;
    s.bytes = ReadBytes(path);
    s.written = data.ftLastWriteTime;
    s.attributes = data.dwFileAttributes;
    return s;
}

// Where Defaults.ini is for each run of comparison 2: at the built-in values, which the first
// load creates, and with the values a player changed, written from it.
fs::path g_builtinDefaults;
fs::path g_alteredDefaults;
// What each Defaults.ini gives: the settings a fresh install starts on over it.
Config g_builtinConfig;
Config g_alteredConfig;

cfg::ConfigOwnerOptions<Config> OwnerOptions(const fs::path& dir, const fs::path& defaults) {
    return MakeConfigOwnerOptions(dir.wstring() + L"\\", cfg::DefaultsFile::At(defaults.wstring()));
}

// The import with its map, for the values it records.
ImportResult RunMappedImport(Scratch& scratch, const Input& input) {
    const fs::path file = Place(scratch.Clean("mapped"), input);
    Config out = MakeConfigTable().defaults();
    return MakeLegacyImport().run(cfg::LegacyInput{file.wstring(), file.string(), false}, out);
}

const DroppedValue* FindDrop(const std::vector<DroppedValue>& dropped, DropRule rule, const char* section,
                             const char* key) {
    for (const DroppedValue& d : dropped) {
        if (d.rule == rule && d.section == section && d.key == key) return &d;
    }
    return nullptr;
}

struct Tally {
    std::string committed;
    std::set<std::string> migrated;
    struct Run {
        int created = 0;
        int imported = 0;
        int refused = 0;
        // Migrated files holding at least one default row.
        int with_default_rows = 0;
        // Migrated files holding a value on at least one row, which the committed file never does.
        int with_values = 0;
    } builtin, altered;
    int with_pose_shaping_dropped = 0;
    int with_reticle_dropped = 0;
    int with_modifier_dropped = 0;
    int with_limit_clamped = 0;
    // Inputs where the player changed at least one row, and where they changed the tracking mode.
    int touched = 0;
    int mode_touched = 0;
};

// A Ctrl, Shift or Alt key alone (N3), which the migration unbinds.
bool IsModifierKey(int vk) { return (vk >= 0x10 && vk <= 0x12) || (vk >= 0xA0 && vk <= 0xA5); }

// PositionScale is listed as the one pose-shaping value, folded where it holds the 100 the
// published build shipped and dropped as PoseShaping where it does not; ShowAimMarker is dropped
// exactly where it was true; a hotkey code on a modifier key alone is dropped as ModifierKey; a
// limit above 10 is dropped as NumberOutOfRange; and nothing is dropped by any other rule.
void CheckDrops(const std::string& name, const legacy::Config& l, const ImportResult& imported, Tally& tally) {
    Check(imported.pose_shaping.size() == 1, name + ": the import lists " +
                                                 std::to_string(imported.pose_shaping.size()) +
                                                 " pose-shaping values, not 1");
    if (imported.pose_shaping.size() != 1) return;
    const bool atShipped = SameBits(l.position_scale, kWorldUnitsPerMetre);
    const cfg::PoseShapingValue& v = imported.pose_shaping[0];
    Check(v.section == "Position" && v.key == "PositionScale", name + ": PositionScale is not listed");
    Check(v.folded == atShipped, name + ": PositionScale is " + (v.folded ? "folded" : "dropped") + " wrongly");
    const bool scaleDropped = FindDrop(imported.dropped, DropRule::PoseShaping, "Position", "PositionScale") != nullptr;
    Check(scaleDropped != atShipped,
          name + ": PositionScale" + (scaleDropped ? " is dropped at its shipped value" : " is changed and not dropped"));
    if (scaleDropped) ++tally.with_pose_shaping_dropped;

    const bool reticle = FindDrop(imported.dropped, DropRule::Reticle, "General", "ShowAimMarker") != nullptr;
    Check(reticle == l.show_aim_marker, name + ": ShowAimMarker dropped does not match its value");
    if (reticle) ++tally.with_reticle_dropped;

    const std::pair<const char*, int> codes[] = {{"Toggle", l.vk_toggle}, {"CycleMode", l.vk_cycle_mode}, {"YawMode", l.vk_yaw_mode}};
    for (const auto& [key, vk] : codes) {
        const bool unbound = FindDrop(imported.dropped, DropRule::ModifierKey, "Hotkeys", key) != nullptr;
        Check(unbound == IsModifierKey(vk), name + ": [Hotkeys] " + key + " dropped as a modifier key does not match its code");
        if (unbound) ++tally.with_modifier_dropped;
    }

    const std::pair<const char*, float> limits[] = {{"LimitX", l.pos_limit_x},
                                                    {"LimitY", l.pos_limit_y},
                                                    {"LimitYDown", l.pos_limit_y_down},
                                                    {"LimitZ", l.pos_limit_z},
                                                    {"LimitZBack", l.pos_limit_z_back}};
    bool clamped = false;
    for (const auto& [key, read] : limits) {
        const bool dropped = FindDrop(imported.dropped, DropRule::NumberOutOfRange, "Position", key) != nullptr;
        Check(dropped == (read > kMaxCanonicalLimit),
              name + ": [Position] " + key + " dropped as out of range does not match its value");
        if (dropped) clamped = true;
    }
    if (clamped) ++tally.with_limit_clamped;

    for (const DroppedValue& d : imported.dropped) {
        Check(d.rule == DropRule::PoseShaping || d.rule == DropRule::Reticle || d.rule == DropRule::ModifierKey ||
                  d.rule == DropRule::NumberOutOfRange,
              name + ": the import drops [" + d.section + "] " + d.key + " by a rule this map never applies");
    }
}

using cfg::schema::Concept;
using ConceptSet = std::set<Concept>;

// Every row of the table that follows Defaults.ini.
const ConceptSet& FollowingRows() {
    static const ConceptSet rows = {
        Concept::UdpPort,           Concept::EnableOnStartup,  Concept::DataFreshnessMs,
        Concept::WorldSpaceYaw,     Concept::RotationEnabled,  Concept::PositionEnabled,
        Concept::LocalSmoothing,    Concept::RemoteSmoothing,  Concept::PositionLimitX,
        Concept::PositionLimitY,    Concept::PositionLimitYDown, Concept::PositionLimitZ,
        Concept::PositionLimitZBack, Concept::CollisionEnabled, Concept::ToggleKey,
        Concept::CycleTrackingModeKey, Concept::YawModeKey,
    };
    return rows;
}

// The rows the player never changed: each reads as the published build shipped it. A hotkey is
// its code and its chord switch together, and the tracking mode is both rows or neither.
ConceptSet UntouchedRows(const legacy::Config& l) {
    const legacy::Config s;
    ConceptSet changed;
    const auto mark = [&changed](bool same, std::initializer_list<Concept> rows) {
        if (!same) changed.insert(rows.begin(), rows.end());
    };
    mark(l.udp_port == s.udp_port, {Concept::UdpPort});
    mark(l.enabled_on_startup == s.enabled_on_startup, {Concept::EnableOnStartup});
    mark(l.data_freshness_ms == s.data_freshness_ms, {Concept::DataFreshnessMs});
    mark(l.world_space_yaw == s.world_space_yaw, {Concept::WorldSpaceYaw});
    mark(l.position_enabled == s.position_enabled, {Concept::RotationEnabled, Concept::PositionEnabled});
    mark(l.local_smoothing == s.local_smoothing, {Concept::LocalSmoothing});
    mark(l.remote_smoothing == s.remote_smoothing, {Concept::RemoteSmoothing});
    mark(l.pos_limit_x == s.pos_limit_x, {Concept::PositionLimitX});
    mark(l.pos_limit_y == s.pos_limit_y, {Concept::PositionLimitY});
    mark(l.pos_limit_y_down == s.pos_limit_y_down, {Concept::PositionLimitYDown});
    mark(l.pos_limit_z == s.pos_limit_z, {Concept::PositionLimitZ});
    mark(l.pos_limit_z_back == s.pos_limit_z_back, {Concept::PositionLimitZBack});
    mark(l.collision_enabled == s.collision_enabled, {Concept::CollisionEnabled});
    mark(l.vk_toggle == s.vk_toggle && l.chord_toggle == s.chord_toggle, {Concept::ToggleKey});
    mark(l.vk_cycle_mode == s.vk_cycle_mode && l.chord_cycle_mode == s.chord_cycle_mode,
         {Concept::CycleTrackingModeKey});
    mark(l.vk_yaw_mode == s.vk_yaw_mode && l.chord_yaw_mode == s.chord_yaw_mode, {Concept::YawModeKey});
    ConceptSet untouched;
    for (const Concept row : FollowingRows()) {
        if (!changed.count(row)) untouched.insert(row);
    }
    return untouched;
}

std::string Names(const ConceptSet& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<std::size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// The settings the mod starts on after the migration against the ones the frozen reader's build
// started on, with the approved changes applied: PositionScale folded (CheckDrops holds the
// import to recording a changed one), no aim marker switch, a hotkey code on a modifier key
// alone unbound (N3), and each limit above 10 at 10 (N4). A row in `follows` runs on `d`, what
// Defaults.ini gives, instead of the legacy value.
std::vector<std::string> StartupDifferences(const legacy::Config& l, const Config& m, const ConceptSet& follows,
                                            const Config& d) {
    std::vector<std::string> diff;
    const auto from = [&follows](Concept row) { return follows.count(row) != 0; };
    const auto flag = [&](Concept row, bool got, bool legacyValue, bool defaultsValue, const char* name) {
        if (got != (from(row) ? defaultsValue : legacyValue)) diff.push_back(name);
    };
    const auto number = [&](Concept row, float got, float legacyValue, float defaultsValue, const char* name) {
        if (!SameBits(got, from(row) ? defaultsValue : legacyValue)) diff.push_back(name);
    };
    flag(Concept::EnableOnStartup, m.enable_on_startup, l.enabled_on_startup, d.enable_on_startup, "EnableOnStartup");
    if (m.udp_port != (from(Concept::UdpPort) ? d.udp_port : l.udp_port)) diff.push_back("UdpPort");
    if (m.data_freshness_ms != (from(Concept::DataFreshnessMs) ? d.data_freshness_ms : l.data_freshness_ms)) {
        diff.push_back("DataFreshnessMs");
    }
    flag(Concept::WorldSpaceYaw, m.world_space_yaw, l.world_space_yaw, d.world_space_yaw, "WorldSpaceYaw");
    if (from(Concept::RotationEnabled) != from(Concept::PositionEnabled)) diff.push_back("half a tracking mode");
    const auto mode = cameraunlock::DecodeTrackingMode(m.rotation_enabled, m.position_enabled);
    const auto wantMode = from(Concept::PositionEnabled)
                              ? cameraunlock::DecodeTrackingMode(d.rotation_enabled, d.position_enabled)
                              : std::optional<cameraunlock::TrackingMode>(
                                    l.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                                       : cameraunlock::TrackingMode::RotationOnly);
    if (!mode || mode != wantMode) diff.push_back("tracking mode");
    number(Concept::LocalSmoothing, m.local_smoothing, l.local_smoothing, d.local_smoothing, "LocalSmoothing");
    number(Concept::LocalSmoothing, m.position.local_smoothing, l.local_smoothing, d.position.local_smoothing,
           "LocalSmoothing (position)");
    number(Concept::RemoteSmoothing, m.remote_smoothing, l.remote_smoothing, d.remote_smoothing, "RemoteSmoothing");
    number(Concept::RemoteSmoothing, m.position.remote_smoothing, l.remote_smoothing, d.position.remote_smoothing,
           "RemoteSmoothing (position)");
    number(Concept::PositionLimitX, m.position.limit_x, ClampedLimit(l.pos_limit_x), d.position.limit_x, "PositionLimitX");
    number(Concept::PositionLimitY, m.position.limit_y, ClampedLimit(l.pos_limit_y), d.position.limit_y, "PositionLimitY");
    number(Concept::PositionLimitYDown, m.position.limit_y_down, ClampedLimit(l.pos_limit_y_down), d.position.limit_y_down,
           "PositionLimitYDown");
    number(Concept::PositionLimitZ, m.position.limit_z, ClampedLimit(l.pos_limit_z), d.position.limit_z, "PositionLimitZ");
    number(Concept::PositionLimitZBack, m.position.limit_z_back, ClampedLimit(l.pos_limit_z_back), d.position.limit_z_back,
           "PositionLimitZBack");
    flag(Concept::CollisionEnabled, m.collision_enabled, l.collision_enabled, d.collision_enabled, "CollisionEnabled");
    if (!SameBits(m.lean_clamp.skin, l.collision_padding)) diff.push_back("CollisionMargin");
    if (!SameBits(m.fov_scale, l.fov_scale)) diff.push_back("FovScale");
    // Each action fires as Defaults.ini binds it where its row follows that file, and as the
    // legacy build fired it everywhere else, with a code on a modifier key alone unbound.
    specops_oracle_view::HotkeyView legacyKeys = KeysOf(l);
    for (int* vk : {&legacyKeys.vk_toggle, &legacyKeys.vk_cycle_mode, &legacyKeys.vk_yaw_mode}) {
        if (IsModifierKey(*vk)) *vk = 0;
    }
    const specops_oracle_view::FireTable legacyFires = specops_oracle_view::OracleFires(legacyKeys);
    const specops_oracle_view::FireTable defaultsFires = CurrentFires(d);
    const Concept keyRows[3] = {Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey};
    specops_oracle_view::FireTable expected = legacyFires;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        for (int action = 0; action < 3; ++action) {
            if (from(keyRows[action])) expected[i][action] = defaultsFires[i][action];
        }
    }
    const specops_oracle_view::FireTable got = CurrentFires(m);
    if (expected != got) diff.push_back("hotkeys: " + FirstFireDifference(expected, got));
    return diff;
}

// Every field the table binds, as the canonical renderer writes it, so two Configs compare whole.
std::string AllValues(const Config& c) {
    return cfg::RenderCanonical(MakeConfigTable(), c, {kConfigDisplayName});
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

using Files = std::vector<std::pair<std::string, std::string>>;

void Comparison2(Scratch& scratch, const Input& input, const ImportRun& import, const ImportResult* mapped,
                 const fs::path& defaults, Tally& tally) {
    const bool builtin = defaults == g_builtinDefaults;
    Tally::Run& run = builtin ? tally.builtin : tally.altered;
    const std::string name =
        input.name + (builtin ? " (Defaults.ini at the built-in values)" : " (Defaults.ini changed)");

    const fs::path dir = scratch.Clean("migration");
    const fs::path config = dir / kConfigFileName;
    const fs::path legacyFile = Place(dir, input);
    const FileStamp defaultsBefore = Stamp(defaults);
    FileStamp legacyBefore;
    if (input.bytes) legacyBefore = Stamp(legacyFile);

    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(OwnerOptions(dir, defaults)).Load();
    const Listing after = List(dir);
    Check(Stamp(defaults) == defaultsBefore, name + ": the load wrote Defaults.ini");
    if (input.bytes) {
        Check(Stamp(legacyFile) == legacyBefore, name + ": SpecOpsTheLineHeadTracking.ini did not keep its bytes, write time and attributes");
    }

    if (!input.bytes) {
        // A fresh install, which follows Defaults.ini.
        ++run.created;
        Check(loaded.status == ConfigLoadStatus::Created, name + ": no file is not Created");
        Check(after.files == Files{{kConfigFileName, tally.committed}},
              name + ": the folder does not hold CameraUnlock.ini as config/CameraUnlock.ini and nothing else");
        if (builtin) {
            const std::vector<std::string> d = StartupDifferences(import.config, loaded.config, FollowingRows(), g_builtinConfig);
            Check(d.empty(), name + ": comparison 2: " + Join(d));
        }
        return;
    }
    if (import.result.status == legacy::ReadStatus::Refused) {
        ++run.refused;
        Check(loaded.status == ConfigLoadStatus::LegacyRefused, name + ": a file the import refuses is not LegacyRefused");
        Check(after.files == Files{{kFileName, *input.bytes}}, name + ": a refused file got a CameraUnlock.ini or another file beside it");
        return;
    }

    const ConceptSet follows(mapped->follows_defaults_ini.begin(), mapped->follows_defaults_ini.end());
    if (builtin) {
        CheckDrops(name, import.config, *mapped, tally);
        Check(follows.size() == mapped->follows_defaults_ini.size(), name + ": follows_defaults_ini names a row twice");
        const ConceptSet untouched = UntouchedRows(import.config);
        Check(follows == untouched, name + ": the import leaves " + Names(follows) +
                                        " to Defaults.ini, and the player never changed " + Names(untouched));
        if (untouched != FollowingRows()) ++tally.touched;
        if (!untouched.count(Concept::PositionEnabled)) ++tally.mode_touched;
    }

    // The session runs on the settings the load hands back: Defaults.ini's on each row the
    // player never changed, the import's on the rest.
    {
        const std::vector<std::string> d =
            StartupDifferences(import.config, loaded.config, follows, builtin ? g_builtinConfig : g_alteredConfig);
        Check(d.empty(), name + ": comparison 2: " + Join(d));
    }

    ++run.imported;
    Check(loaded.status == ConfigLoadStatus::Migrated,
          name + ": the migration is " + cfg::ConfigLoadStatusName(loaded.status) + ": " + loaded.reason);
    if (loaded.status != ConfigLoadStatus::Migrated) return;
    Check(after.files.size() == 2 && after.files[0].first == kConfigFileName && after.files[1].first == kFileName &&
              after.files[1].second == *input.bytes,
          name + ": the folder does not hold SpecOpsTheLineHeadTracking.ini and CameraUnlock.ini and nothing else");
    Check(Contains(loaded.log, "created from"), name + ": the log does not say where CameraUnlock.ini came from");
    const std::string migrated = ReadBytes(config);
    tally.migrated.insert(migrated);
    for (const Concept row : follows) {
        const std::string key = cfg::schema::kConcepts[static_cast<std::size_t>(row)].key;
        Check(migrated.find("\r\n" + key + "=default\r\n") != std::string::npos,
              name + ": " + key + ", which the player never changed, is not written default");
    }
    if (migrated.find("=default\r\n") != std::string::npos) ++run.with_default_rows;
    if (migrated != tally.committed) ++run.with_values;

    // The next launch reads CameraUnlock.ini over the same Defaults.ini, with nothing to report,
    // to the same settings, does not import, and writes neither file.
    {
        const cfg::ConfigLoadResult<Config> reread = cfg::ConfigOwner<Config>(OwnerOptions(dir, defaults)).Load();
        Check(reread.status == ConfigLoadStatus::Canonical && reread.diagnostics.empty(),
              name + ": the next launch does not read CameraUnlock.ini cleanly");
        Check(AllValues(reread.config) == AllValues(loaded.config), name + ": the next launch runs on other settings");
        Check(!Contains(reread.log, "created from"), name + ": the next launch imports again");
        Check(Contains(reread.log, "is left as it was and is not read"),
              name + ": the next launch does not say SpecOpsTheLineHeadTracking.ini is not read");
        Check(List(dir) == after && Stamp(legacyFile) == legacyBefore && Stamp(defaults) == defaultsBefore,
              name + ": the next launch changed a file");
    }

    // A read-only SpecOpsTheLineHeadTracking.ini imports as a writable one does and keeps its attribute,
    // bytes and write time.
    if (builtin) {
        const fs::path roDir = scratch.Clean("read-only");
        const fs::path roLegacy = Place(roDir, input);
        SetReadOnly(roLegacy, true);
        const FileStamp roBefore = Stamp(roLegacy);
        const cfg::ConfigLoadResult<Config> fromReadOnly = cfg::ConfigOwner<Config>(OwnerOptions(roDir, defaults)).Load();
        Check(fromReadOnly.status == ConfigLoadStatus::Migrated && AllValues(fromReadOnly.config) == AllValues(loaded.config) &&
                  ReadBytes(roDir / kConfigFileName) == migrated,
              name + ": a read-only SpecOpsTheLineHeadTracking.ini does not import as a writable one does");
        Check(Stamp(roLegacy) == roBefore && (roBefore.attributes & FILE_ATTRIBUTE_READONLY) != 0,
              name + ": a read-only SpecOpsTheLineHeadTracking.ini did not keep its attribute, bytes and write time");
    }
}

// Defaults.ini as a player may have changed it, from the one the owner created: every value this
// game takes from it differs from the built-in one, each set to the corpus's alternate for the
// legacy key it comes from, so a corpus input holding that alternate migrates as default.
void WriteAlteredDefaults() {
    std::string text = ReadBytes(g_builtinDefaults);
    const std::pair<const char*, const char*> changes[] = {
        {"UdpPort=4242", "UdpPort=4243"},
        {"EnableOnStartup=true", "EnableOnStartup=false"},
        {"DataFreshnessMs=500", "DataFreshnessMs=250"},
        {"WorldSpaceYaw=true", "WorldSpaceYaw=false"},
        {"PositionEnabled=true", "PositionEnabled=false"},
        {"LocalSmoothing=0.0", "LocalSmoothing=0.3"},
        {"RemoteSmoothing=0.15", "RemoteSmoothing=0.3"},
        {"PositionLimitX=0.3", "PositionLimitX=0.5"},
        {"PositionLimitY=0.2", "PositionLimitY=0.5"},
        {"PositionLimitYDown=0.2", "PositionLimitYDown=0.3"},
        {"PositionLimitZ=0.4", "PositionLimitZ=0.5"},
        {"PositionLimitZBack=0.1", "PositionLimitZBack=0.2"},
        {"CollisionEnabled=true", "CollisionEnabled=false"},
        {"ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=F1, Ctrl+Shift+Y"},
        {"CycleTrackingModeKey=PageUp, Ctrl+Shift+G", "CycleTrackingModeKey=F2, Ctrl+Shift+G"},
        {"YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=F3, Ctrl+Shift+H"},
    };
    for (const auto& [from, to] : changes) {
        const std::string line = std::string("\r\n") + from + "\r\n";
        const size_t at = text.find(line);
        if (at == std::string::npos) throw std::runtime_error(std::string("the created Defaults.ini has no line ") + from);
        text.replace(at + 2, std::strlen(from), to);
    }
    fs::create_directories(g_alteredDefaults.parent_path());
    WriteBytes(g_alteredDefaults, text);
}

std::vector<Input> Inputs(const std::string& firstRun) {
    using cameraunlock::config::testing::GenerateIniMutations;
    std::vector<Input> inputs;
    inputs.push_back({"no file", std::nullopt});
    inputs.push_back({"empty file", std::string()});
    inputs.push_back({"dev first-run output", firstRun});
    for (auto& m : GenerateIniMutations(firstRun, legacy::ReadKeys(), MutationKeys())) {
        inputs.push_back({"corpus: " + m.name, std::move(m.bytes)});
    }
    return inputs;
}

}  // namespace

int main(int argc, char** argv) {
    // How data/dev-first-run.ini was extracted.
    if (argc == 3 && std::strcmp(argv[1], "--write-first-run") == 0) {
        return specops_oracle_view::RunOracle(argv[2]).loaded ? 0 : 1;
    }
    try {
        Scratch scratch;
        Tally tally;
        tally.committed = ReadBytes(fs::path(SPECOPS_COMMITTED_CONFIG));
        Check(!tally.committed.empty(), "config/CameraUnlock.ini is missing");

        // Each Defaults.ini sits outside the game folder, in a user folder of its own whose
        // parent exists, as the owner requires before it creates the file.
        g_builtinDefaults = scratch.Clean("user-builtin") / "CameraUnlock" / "Defaults.ini";
        g_alteredDefaults = scratch.Clean("user-altered") / "CameraUnlock" / "Defaults.ini";
        {
            const fs::path dir = scratch.Clean("first-load");
            Check(cfg::ConfigOwner<Config>(OwnerOptions(dir, g_builtinDefaults)).Load().status == ConfigLoadStatus::Created,
                  "the first load is not Created");
            Check(fs::exists(g_builtinDefaults), "the first load did not create Defaults.ini");
        }
        WriteAlteredDefaults();
        g_builtinConfig = cfg::ConfigOwner<Config>(OwnerOptions(scratch.Clean("builtin-fresh"), g_builtinDefaults)).Load().config;
        g_alteredConfig = cfg::ConfigOwner<Config>(OwnerOptions(scratch.Clean("altered-fresh"), g_alteredDefaults)).Load().config;

        // The dev build's first-run output, committed once as test data, is what the oracle
        // still writes for a missing file.
        const std::string firstRun = ReadBytes(fs::path(SPECOPS_DIFFERENTIAL_DATA) / "dev-first-run.ini");
        Check(!firstRun.empty(), "data/dev-first-run.ini is missing");
        {
            const fs::path file = scratch.Clean("first-run") / kFileName;
            Check(specops_oracle_view::RunOracle(file.string()).loaded, "the oracle did not load its own first run");
            Check(ReadBytes(file) == firstRun, "the oracle's first-run output differs from data/dev-first-run.ini");
        }

        // Fresh equals upgrade: over Defaults.ini at the built-in values, the dev build's
        // first-run output imports into a CameraUnlock.ini that is the committed file, which is
        // what a fresh install creates.
        {
            const fs::path dir = scratch.Clean("fresh-equals-upgrade");
            WriteBytes(dir / kFileName, firstRun);
            Check(cfg::ConfigOwner<Config>(OwnerOptions(dir, g_builtinDefaults)).Load().status == ConfigLoadStatus::Migrated &&
                      ReadBytes(dir / kConfigFileName) == tally.committed,
                  "the dev first-run output does not import into the committed file");
        }

        const std::vector<Input> inputs = Inputs(firstRun);
        std::printf("%zu inputs\n", inputs.size());
        std::printf("comparison 1, the oracle (dev 3e9a526) against the import\n");
        for (const Input& input : inputs) {
            const ImportRun import = Comparison1(scratch, input);
            std::optional<ImportResult> mapped;
            if (input.bytes && import.result.status != legacy::ReadStatus::Refused) {
                mapped = RunMappedImport(scratch, input);
                Check(mapped->status == ImportStatus::Imported, input.name + ": the mapped import is not Imported");
                if (input.name == "empty file" || input.name == "dev first-run output") {
                    Check(ConceptSet(mapped->follows_defaults_ini.begin(), mapped->follows_defaults_ini.end()) ==
                              FollowingRows(),
                          input.name + ": not every row follows Defaults.ini");
                }
            }
            for (const fs::path& defaults : {g_builtinDefaults, g_alteredDefaults}) {
                Comparison2(scratch, input, import, mapped ? &*mapped : nullptr, defaults, tally);
            }
        }

        std::printf("comparison 2, the import against the migration, %zu distinct files:\n", tally.migrated.size());
        for (const auto& [over, run] : {std::pair<const char*, const Tally::Run*>{"at the built-in values", &tally.builtin},
                                        std::pair<const char*, const Tally::Run*>{"changed", &tally.altered}}) {
            std::printf("  over Defaults.ini %s: %d created, %d imported (%d holding a default row, %d a value), "
                        "%d refused as dev refused them\n",
                        over, run->created, run->imported, run->with_default_rows, run->with_values, run->refused);
            Check(run->refused > 0, std::string("no input is refused over ") + over);
            Check(run->with_default_rows > 0, std::string("no import writes default over ") + over);
            Check(run->with_values > 0, std::string("no import writes a value over ") + over);
        }
        std::printf("  %d with a changed PositionScale dropped (pose_shaping)\n",
                    tally.with_pose_shaping_dropped);
        std::printf("  %d with ShowAimMarker=true dropped (reticle)\n", tally.with_reticle_dropped);
        std::printf("  %d hotkey codes on a modifier key alone unbound (N3)\n", tally.with_modifier_dropped);
        std::printf("  %d with a [Position] limit above 10 at 10 (N4)\n", tally.with_limit_clamped);
        Check(tally.with_limit_clamped > 0, "no input clamps a limit");
        Check(tally.with_pose_shaping_dropped > 0, "no input drops a changed PositionScale");
        Check(tally.with_reticle_dropped > 0, "no input drops ShowAimMarker");
        std::printf("  %d with a row the player changed, %d of them the tracking mode\n", tally.touched,
                    tally.mode_touched);
        Check(tally.touched > 0 && tally.mode_touched > 0, "no input changes a row, or none the tracking mode");
        Check(tally.migrated.count(tally.committed) == 1, "no input migrated to the committed file");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path lintDir = fs::path(exe).parent_path() / "migrated";
        fs::remove_all(lintDir);
        fs::create_directories(lintDir);
        int n = 0;
        for (const std::string& file : tally.migrated) WriteBytes(lintDir / (std::to_string(n++) + ".ini"), file);
    } catch (const std::exception& e) {
        std::printf("  FAIL: threw: %s\n", e.what());
        ++g_failures;
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
