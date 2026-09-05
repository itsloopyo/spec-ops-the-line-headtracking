#pragma once

#include "MinHook.h"

namespace SpecOpsTheLineHeadTracking {

// Create then enable, the two-step every hook in this mod goes through. Returns the
// first status that was not MH_OK so each caller keeps its own wording and its own
// severity: a failed camera hook aborts the install, a failed crosshair, aim-trace or
// overlay hook only costs the reticle.
inline MH_STATUS CreateAndEnableHook(void* target, void* detour, void** original) {
    const MH_STATUS created = MH_CreateHook(target, detour, original);
    if (created != MH_OK) {
        return created;
    }
    return MH_EnableHook(target);
}

}  // namespace SpecOpsTheLineHeadTracking
