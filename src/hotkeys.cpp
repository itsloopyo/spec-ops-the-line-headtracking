#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace SpecOpsTheLineHeadTracking {

namespace {

// The config table already refused a list that does not parse, so one here is a bug rather
// than a player's typo.
std::vector<cameraunlock::input::KeyBinding> Parse(const std::string& list) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error("hotkey list '" + list + "' does not parse: " + parsed.error);
    return parsed.bindings;
}

}  // namespace

bool Hotkeys::Start(const Config& cfg, Action onToggle,
                    Action onCycleMode, Action onYawMode) {
    if (m_started.load(std::memory_order_acquire)) return true;

    // One registration per key: a binding without modifiers stays quiet while Ctrl and
    // Shift are both held, so Ctrl+Shift with a key reaches only a binding that names it,
    // and one press never fires an action twice.
    using cameraunlock::input::RegisterKeyBindings;
    RegisterKeyBindings(m_poller, Parse(cfg.toggle_key_name), std::move(onToggle));
    RegisterKeyBindings(m_poller, Parse(cfg.cycle_tracking_mode_key_name), std::move(onCycleMode));
    RegisterKeyBindings(m_poller, Parse(cfg.yaw_mode_key_name), std::move(onYawMode));

    // The poller rethrows rather than failing silently when the thread cannot be
    // created. This entry point is reached from a __stdcall thread procedure, where an
    // escaping exception is std::terminate: the game would vanish during startup with
    // the log ending on an unrelated line. Caught here so the reason is written down
    // and the caller can tear tracking back down and stay dormant.
    try {
        if (!m_poller.Start(16)) {
            Log::Line("ERROR: HotkeyPoller failed to start");
            return false;
        }
    } catch (const std::exception& e) {
        Log::Line("ERROR: HotkeyPoller failed to start: %s", e.what());
        return false;
    }

    Log::Line("Hotkeys: toggle=[%s] cycle mode=[%s] yaw mode=[%s]",
              cfg.toggle_key_name.c_str(), cfg.cycle_tracking_mode_key_name.c_str(),
              cfg.yaw_mode_key_name.c_str());

    m_started.store(true, std::memory_order_release);
    return true;
}

void Hotkeys::Stop() {
    if (m_started.exchange(false, std::memory_order_acq_rel)) {
        m_poller.Stop();
    }
}

}
