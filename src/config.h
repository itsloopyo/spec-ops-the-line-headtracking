#pragma once

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/head_tracking_config.h"
#include "cameraunlock/config/legacy_import.h"

#include <string>

namespace SpecOpsTheLineHeadTracking {

constexpr const char* kConfigFileName = "CameraUnlock.ini";
// The file every build before the canonical format read, beside kConfigFileName. Imported once
// while kConfigFileName is absent, and never written.
constexpr const char* kLegacyConfigFileName = "SpecOpsTheLineHeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Spec Ops: The Line";

// World units (cm) per metre of head translation. The UE3 world is centimetres, and this is the
// PositionScale every build before the canonical format shipped.
constexpr float kWorldUnitsPerMetre = 100.0f;

// Core's config with this game's defaults.
struct Config : cameraunlock::HeadTrackingConfig {
    // Multiplies the field of view the scene view is projected with. 1.0 is the game's own.
    // Only the rendered image widens: the two game-logic callers of the same accessor keep the
    // game's value, and the reticle projection reads the number the frame was actually
    // projected with, so it follows automatically.
    float fov_scale = 1.0f;

    Config() {
        // World units (cm) held between the leaned eye and the surface it stopped at.
        lean_clamp.skin = 10.0f;
    }
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable:
// the mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// SpecOpsTheLineHeadTracking.ini as the builds before the canonical format read it
// (legacy_config/), mapped into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for the files in `folder` (with its trailing separator): the settings in
// CameraUnlock.ini, imported once from SpecOpsTheLineHeadTracking.ini. The mod passes
// DefaultsFile::PerUser() and a test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                        cameraunlock::config::DefaultsFile defaults);

}  // namespace SpecOpsTheLineHeadTracking
