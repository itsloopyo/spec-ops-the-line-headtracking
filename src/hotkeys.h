#pragma once

#include "config.h"

#include "cameraunlock/input/hotkey_poller.h"

#include <atomic>
#include <functional>

namespace SpecOpsTheLineHeadTracking {

class Hotkeys {
public:
    using Action = std::function<void()>;

    bool Start(const Config& cfg, Action onToggle,
               Action onCycleMode, Action onYawMode);
    void Stop();

private:
    cameraunlock::input::HotkeyPoller m_poller;
    // Both the init thread and DLL_PROCESS_DETACH can call Stop(). The poller's own
    // Stop() tests a flag and then joins, so two callers reaching it together join one
    // std::thread twice; claiming this flag leaves exactly one of them to do it.
    std::atomic<bool> m_started{false};
};

}
