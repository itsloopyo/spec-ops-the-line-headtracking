// Compiled into the config oracle library only, with `cameraunlock` and
// `SpecOpsTheLineHeadTracking` renamed, so "config.h" here is the published build's
// (oracle/src/config.h).
#include "config.h"
#include "oracle_adapter.h"

namespace specops_oracle_view {

OracleResult RunOracle(const std::string& path) {
    SpecOpsTheLineHeadTracking::Config c;
    const bool loaded = c.LoadOrCreate(path.c_str());
    OracleConfig o{};
    o.enabled_on_startup = c.enabled_on_startup;
    o.udp_port = c.udp_port;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.show_aim_marker = c.show_aim_marker;
    o.data_freshness_ms = c.data_freshness_ms;
    o.world_space_yaw = c.world_space_yaw;
    o.fov_scale = c.fov_scale;
    o.position_enabled = c.position_enabled;
    o.pos_limit_x = c.pos_limit_x;
    o.pos_limit_y = c.pos_limit_y;
    o.pos_limit_y_down = c.pos_limit_y_down;
    o.pos_limit_z = c.pos_limit_z;
    o.pos_limit_z_back = c.pos_limit_z_back;
    o.position_scale = c.position_scale;
    o.collision_enabled = c.collision_enabled;
    o.collision_padding = c.collision_padding;
    o.vk_toggle = c.vk_toggle;
    o.vk_cycle_mode = c.vk_cycle_mode;
    o.vk_yaw_mode = c.vk_yaw_mode;
    o.chord_toggle = c.chord_toggle;
    o.chord_cycle_mode = c.chord_cycle_mode;
    o.chord_yaw_mode = c.chord_yaw_mode;
    return {loaded, o};
}

}
