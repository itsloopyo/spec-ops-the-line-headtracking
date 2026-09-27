// Compiled into the hotkey oracle library only, with `cameraunlock` and
// `SpecOpsTheLineHeadTracking` renamed, so "hotkeys.h" here is the published build's Hotkeys and
// the poller under it is oracle_fake's.
#include "hotkeys.h"
#include "oracle_adapter.h"

#include <stdexcept>

namespace specops_oracle_view {

FireTable OracleFires(const HotkeyView& keys) {
    namespace input = cameraunlock::input;
    SpecOpsTheLineHeadTracking::Config c;
    c.vk_toggle = keys.vk_toggle;
    c.vk_cycle_mode = keys.vk_cycle_mode;
    c.vk_yaw_mode = keys.vk_yaw_mode;
    c.chord_toggle = keys.chord_toggle;
    c.chord_cycle_mode = keys.chord_cycle_mode;
    c.chord_yaw_mode = keys.chord_yaw_mode;

    std::array<int, 3> fired{};
    input::FakeRegistrations().clear();
    SpecOpsTheLineHeadTracking::Hotkeys hotkeys;
    if (!hotkeys.Start(c, [&fired] { ++fired[0]; }, [&fired] { ++fired[1]; }, [&fired] { ++fired[2]; })) {
        throw std::logic_error("the published Hotkeys::Start failed on the fake poller");
    }
    const std::vector<input::FakeRegistration> registered = input::FakeRegistrations();
    hotkeys.Stop();

    // The published poller's Poll: a callback runs when its nonzero key goes down.
    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            fired = {};
            input::FakeHeld() = held;
            for (const input::FakeRegistration& r : registered) {
                if (r.vk == vk && r.callback) r.callback();
            }
            table.push_back(fired);
        }
    }
    input::FakeHeld() = 0;
    return table;
}

}
