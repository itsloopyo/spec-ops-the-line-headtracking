#include "build_profile.h"

#include "logging.h"

#include <windows.h>

namespace SpecOpsTheLineHeadTracking {

// steam-win32-20120716: Steam retail build, EXE linked 2012-07-16. ImageBase
// 0x400000, no ASLR.
//   GetPlayerViewPoint  file VA 0x00AB83F0 - reached from 21 call sites.
//   scene-view caller   file VA 0x0075C607 - the return address inside
//                       ULocalPlayer::CalcSceneView (0x0075C460), which builds
//                       the view matrix from the viewpoint it just fetched.
//   GetFOVAngle         file VA 0x006F1EA0 - three direct callers, no vtable
//                       references. CalcSceneView's is the `call` at 0x0075C60A,
//                       two instructions after the viewpoint call, so the return
//                       address is 0x0075C60F. It multiplies the result by
//                       0.008726646 (0.5 * PI/180) and hands that to
//                       FPerspectiveMatrix along with the constrained aspect
//                       ([camera+0x210]) or the viewport size, so the accessor
//                       returns the full HORIZONTAL field of view in degrees.
//                       The other two callers (0x00D256ED, 0x00D28D8B) are game
//                       logic and are left on the game's own value.
//   UYCrosshair::Draw   file VA 0x00C079F0 - __thiscall(AYHUD*, FLOAT), ret 8.
//                       Sits at slot +0x130 of the nine UYCrosshair vtables in .rdata
//                       (0x012D9F10 and every 0x148 after it) and forwards to slot
//                       +0x134, the subclass's DrawCrosshair.
//   SingleLineCheck     file VA 0x009BDE30 - the world line check
//                       AActor::execFastTrace @ 0x0083CA90 calls with GWorld
//                       (0x014DF734) in ecx. Seven stack arguments: ret 0x1c.
static const BuildProfile kSteamProfile_20120716 = {
    "steam-win32-20120716",
    { 0x50041F0Cu, 0x012C7000u, 0x011DEB97u },
    0x6B83F0u,
    0x35C607u,
    0x2F1EA0u,
    0x35C60Fu,
    0x8079F0u,
    0x5BDE30u,
    0x10DF734u,
};

const BuildProfile kKnownProfiles[] = {
    kSteamProfile_20120716,
};
const int kKnownProfileCount = static_cast<int>(sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]));

const BuildProfile* MatchRunningProfile() {
    HMODULE hExe = GetModuleHandleA("SpecOpsTheLine.exe");
    if (!hExe) {
        Log::Line("ERROR: SpecOpsTheLine.exe module not found for fingerprinting");
        return nullptr;
    }

    cameraunlock::memory::PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(hExe, running)) {
        Log::Line("ERROR: could not read PE fingerprint of SpecOpsTheLine.exe");
        return nullptr;
    }

    for (int i = 0; i < kKnownProfileCount; ++i) {
        if (running.Matches(kKnownProfiles[i].fingerprint)) {
            Log::Line("Build profile matched: %s", kKnownProfiles[i].name);
            return &kKnownProfiles[i];
        }
    }

    using cameraunlock::memory::ClassifyMismatch;
    using cameraunlock::memory::FingerprintMismatch;
    const BuildProfile& primary = kKnownProfiles[0];
    switch (ClassifyMismatch(running, primary.fingerprint)) {
        case FingerprintMismatch::Newer:
            Log::Line("Unrecognised Spec Ops: The Line build (newer than %s). "
                      "Check the releases page for an updated mod. Staying dormant.",
                      primary.name);
            break;
        case FingerprintMismatch::Older:
            Log::Line("Unrecognised Spec Ops: The Line build (older than %s). "
                      "Let Steam finish updating. Staying dormant.",
                      primary.name);
            break;
        case FingerprintMismatch::Differs:
            Log::Line("SpecOpsTheLine.exe is tampered/repacked (fingerprint differs). "
                      "Staying dormant.");
            break;
    }
    return nullptr;
}

}  // namespace SpecOpsTheLineHeadTracking
