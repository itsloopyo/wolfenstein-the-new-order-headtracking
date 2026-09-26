// Compiled into the config oracle library only, with `cameraunlock` and `wolf_ht` renamed, so
// "config.h" here is the published build's (oracle/src/config.h).
#include "config.h"
#include "oracle_adapter.h"

namespace wolf_oracle_view {

OracleConfig RunOracle(const std::string& dir) {
    wolf_ht::Config c;
    wolf_ht::WriteDefaultConfigIfMissing(dir);
    wolf_ht::LoadConfig(dir, c);
    OracleConfig o{};
    o.udp_port = c.udp_port;
    o.enable_on_startup = c.enable_on_startup;
    o.toggle_key = c.toggle_key;
    o.cycle_mode_key = c.cycle_mode_key;
    o.yaw_mode_key = c.yaw_mode_key;
    o.ads_mode_key = c.ads_mode_key;
    o.chord_toggle_key = c.chord_toggle_key;
    o.chord_cycle_mode_key = c.chord_cycle_mode_key;
    o.chord_yaw_mode_key = c.chord_yaw_mode_key;
    o.chord_ads_mode_key = c.chord_ads_mode_key;
    o.world_space_yaw = c.world_space_yaw;
    o.ads_mode = static_cast<int>(c.ads_mode);
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.position_enabled = c.position_enabled;
    o.limit_x = c.limit_x;
    o.limit_y = c.limit_y;
    o.limit_z = c.limit_z;
    o.limit_z_back = c.limit_z_back;
    return o;
}

}  // namespace wolf_oracle_view
