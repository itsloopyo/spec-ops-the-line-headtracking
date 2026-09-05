#include "aim_trace.h"
#include "build_profile.h"
#include "camera_hook.h"
#include "config.h"
#include "crosshair_hook.h"
#include "hotkeys.h"
#include "logging.h"
#include "path_utils.h"
#include "game_state.h"
#include "reticle_overlay.h"
#include "tracking_runtime.h"

#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/memory/pe_fingerprint.h"

#include <windows.h>
#include <process.h>

#include <string>

namespace {

using namespace SpecOpsTheLineHeadTracking;

constexpr const char* kModName    = "SpecOpsTheLineHeadTracking";
constexpr const char* kModVersion = "0.0.0";
constexpr const char* kGameExe    = "SpecOpsTheLine.exe";

// All three live next to the game EXE, alongside the loader.
constexpr const char* kIniFileName     = "SpecOpsTheLineHeadTracking.ini";
constexpr const char* kLogFileName     = "SpecOpsTheLineHeadTracking.log";

constexpr int kInitMaxWaitMs   = 30000;
constexpr int kInitPollMs      = 100;
constexpr int kHeartbeatMs     = 5000;
constexpr int kOverlayAttachWaitMs = 120000;

// WinDrv registers this class for the game viewport. Its window appearing is the
// point at which the engine has finished booting and the camera exists.
constexpr const char* kEngineWindowClass = "LaunchUnrealUWindowsClient";
constexpr int kEngineWaitMaxMs = 180000;
constexpr LONG kMinViewportWidth  = 320;
constexpr LONG kMinViewportHeight = 240;

HANDLE g_initThreadHandle = nullptr;
// Signalled on DLL_PROCESS_DETACH. The heartbeat waits on it instead of sleeping so
// unload does not have to outlast a 5s Sleep: it used to give the thread 2s, then pull
// the hooks out and close the log while the heartbeat was still asleep and about to
// read both.
HANDLE g_shutdownEvent = nullptr;

TrackingRuntime g_tracking;
Hotkeys         g_hotkeys;

// Every wait in this file goes through here, so unload never has to outlast a poll
// interval: the event is signalled on detach and each loop unwinds within kInitPollMs
// instead of running its 30s or 180s course. Returns false when the DLL is unloading.
bool SleepUnlessUnloading(int ms) {
    return WaitForSingleObject(g_shutdownEvent, static_cast<DWORD>(ms)) == WAIT_TIMEOUT;
}

void LogFingerprint() {
    HMODULE hExe = GetModuleHandleA(kGameExe);
    cameraunlock::memory::PeFingerprint fp{};
    if (hExe && cameraunlock::memory::ReadPeFingerprint(hExe, fp)) {
        Log::Line("PE fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
                  fp.TimeDateStamp, fp.SizeOfImage, fp.CheckSum);
    } else {
        Log::Line("WARN: could not read PE fingerprint of %s", kGameExe);
    }
}

// The mod is loaded before the game image is, so nothing can be fingerprinted or hooked
// until it appears. Each wait reports its own timeout, because a wait that was cut short
// by unload has not timed out and must not claim it did.
bool WaitForGameModule() {
    for (int waited = 0; waited < kInitMaxWaitMs; waited += kInitPollMs) {
        if (GetModuleHandleA(kGameExe)) {
            return true;
        }
        if (!SleepUnlessUnloading(kInitPollMs)) {
            return false;
        }
    }
    Log::Line("ERROR: %s never appeared after %ds; nothing to hook",
              kGameExe, kInitMaxWaitMs / 1000);
    return false;
}

// The window handle exists while it is still a 160x28 stub, well before the
// engine has a camera, so wait for it to reach viewport size instead.
bool WaitForEngineWindow() {
    for (int waited = 0; waited < kEngineWaitMaxMs; waited += kInitPollMs) {
        HWND hwnd = FindWindowA(kEngineWindowClass, nullptr);
        RECT client{};
        if (hwnd && GetClientRect(hwnd, &client) &&
            client.right >= kMinViewportWidth && client.bottom >= kMinViewportHeight) {
            Log::Line("Engine viewport up (%ldx%ld) after %.1fs",
                      client.right, client.bottom, waited / 1000.0);
            return true;
        }
        if (!SleepUnlessUnloading(kInitPollMs)) {
            return false;
        }
    }
    Log::Line("ERROR: engine render window never appeared after %ds; no camera hook",
              kEngineWaitMaxMs / 1000);
    return false;
}

void OpenSessionLog() {
    // Core opens with CREATE_ALWAYS, so the log holds this session and nothing
    // else, and renames the outgoing one to .prev.log first: the session worth
    // reading is usually the one that just crashed, and the user relaunches the
    // game before fetching the file. A rename that fails is reported there.
    const std::wstring logPath = GetModulePathW(kLogFileName);
    if (logPath.empty()) {
        // Nothing else in this file can report anything once this has failed, so say
        // so where a debugger or DebugView can still see it.
        OutputDebugStringW(L"SpecOpsTheLineHeadTracking: could not resolve its own "
                           L"module directory; no log file this session\n");
        return;
    }
    Log::Open(logPath);
    cameraunlock::diagnostics::InstallCrashHandler();
}

bool LoadConfig(Config& cfg) {
    const std::string iniPath = GetModulePath(kIniFileName);
    if (iniPath.empty()) {
        // Never fall back to the bare filename: GetPrivateProfileString resolves a
        // relative path against the Windows directory, so the mod would read an INI
        // that is not the user's and report every setting as its default.
        Log::Line("ERROR: could not resolve the path to %s beside this DLL; "
                  "staying dormant", kIniFileName);
        return false;
    }
    if (!cfg.LoadOrCreate(iniPath.c_str())) {
        Log::Line("ERROR: Config load failed");
        return false;
    }
    Log::Line("Config: port=%u enabled=%d smoothing=local %.2f/remote %.2f "
              "position=%d fov scale=%.3f",
              cfg.udp_port, cfg.enabled_on_startup ? 1 : 0,
              cfg.local_smoothing, cfg.remote_smoothing,
              cfg.position_enabled ? 1 : 0, cfg.fov_scale);
    return true;
}

bool StartTrackingAndHotkeys(const Config& cfg) {
    g_tracking.Start(cfg);
    if (!g_hotkeys.Start(cfg,
                         [] { g_tracking.ToggleEnabled(); },
                         [] { g_tracking.CycleTrackingMode(); },
                         [] { g_tracking.ToggleYawMode(); })) {
        Log::Line("ERROR: Hotkeys start failed");
        g_tracking.Stop();
        return false;
    }
    return true;
}

void StopTrackingAndHotkeys() {
    g_hotkeys.Stop();
    g_tracking.Stop();
}

// The camera does not exist until the engine has built its viewport, so every hook that
// patches the game image waits for the render window rather than going in from DllMain.
// The crosshair and aim-trace hooks report their own failures and cost only the reticle,
// so neither aborts the install.
bool InstallGameHooks(const BuildProfile& profile, const Config& cfg) {
    if (!WaitForEngineWindow()) {
        return false;
    }

    const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(kGameExe));
    InitGameState(moduleBase + profile.rvaWorldPtr);

    const CameraHookTargets targets = {
        moduleBase + profile.rvaGetPlayerViewPoint,
        moduleBase + profile.rvaSceneViewCallSite,
        moduleBase + profile.rvaGetFovAngle,
        moduleBase + profile.rvaFovCallSite,
    };
    if (!InstallCameraHook(targets, g_tracking, cfg)) {
        Log::Line("ERROR: Camera hook install failed");
        return false;
    }

    InstallCrosshairHook(moduleBase + profile.rvaCrosshairDraw);
    InstallAimTrace(moduleBase + profile.rvaSingleLineCheck,
                    moduleBase + profile.rvaWorldPtr);
    return true;
}

// Edge-triggered health reporting, once every kHeartbeatMs. Between them these lines
// answer the questions a "no head tracking" report needs - is the camera hook firing, is
// tracker data arriving, is the crosshair moving - and a level-triggered version of any
// of them would add 720 lines an hour that say nothing new. Each latch therefore fires
// at most once, or once per change of state.
class Heartbeat {
public:
    void Tick() {
        ReportCameraDetour();
        ReportTracker();
        ReportCrosshair();
        ReportAimTrace();
        ReportOverlayAttach();
        m_firstReport = false;
    }

private:
    void ReportCameraDetour() {
        const unsigned long calls = CameraHookCallCount();
        const bool detouring = calls > 0;
        if (m_firstReport || detouring != m_lastDetouring) {
            Log::Line("Camera detour: %s (render calls=%lu)",
                      detouring ? "firing" : "not firing", calls);
            m_lastDetouring = detouring;
        }
        // The viewpoint accessor running while the scene-view caller never does
        // means the renderer took its other branch, so nothing will be tracked.
        if (!detouring && !m_warnedNoRenderCaller && CameraHookNonRenderCallCount() > 0) {
            m_warnedNoRenderCaller = true;
            Log::Line("WARN: GetPlayerViewPoint is being called, but never from the "
                      "scene-view call site this profile pins. The rendered view is "
                      "not being tracked.");
        }
        // Same failure one accessor along, but gated on the scene view having been
        // built rather than on the accessor having run at all: the builder calls
        // GetFOVAngle two instructions after the viewpoint, so a render call with no
        // field-of-view call behind it means the pinned call site is wrong. Gating on
        // the accessor alone would fire during startup, when the other two callers run
        // before the first frame is built.
        if (!m_warnedNoFovCaller && detouring && FovHookRenderCallCount() == 0) {
            m_warnedNoFovCaller = true;
            Log::Line("WARN: GetFOVAngle is being called, but never from the scene-view "
                      "call site this profile pins. The reticle is projected with a "
                      "default field of view and FieldOfView.Scale does nothing.");
        }
    }

    void ReportTracker() {
        const bool receiving = g_tracking.IsReceiving();
        if (m_firstReport || receiving != m_lastReceiving) {
            Log::Line("OpenTrack: %s", receiving ? "receiving data" : "no data");
            m_lastReceiving = receiving;
        }
    }

    // Two separate edges, because the first crosshair of a session is normally drawn
    // before any tracker packet arrives: one line saying the hook sees the draw at
    // all, and one saying it moved it. Reporting them together reads as "0 moved"
    // forever, which looks like a broken hook and is only a quiet tracker.
    void ReportCrosshair() {
        if (!m_crosshairDrawReported && CrosshairDrawCount() > 0) {
            m_crosshairDrawReported = true;
            Log::Line("Crosshair draw seen (%lu draws)", CrosshairDrawCount());
        }
        if (!m_crosshairMoveReported && CrosshairMovedCount() > 0) {
            m_crosshairMoveReported = true;
            Log::Line("Crosshair moved onto the aim point (%lu draws)",
                      CrosshairMovedCount());
        }
        if (!m_crosshairSuppressReported && CrosshairSuppressedCount() > 0) {
            m_crosshairSuppressReported = true;
            Log::Line("Crosshair hidden on frames whose aim point left the frame "
                      "(%lu draws). A crosshair drawn anyway would claim the shot lands "
                      "where it is drawn.", CrosshairSuppressedCount());
        }
    }

    // Two edges, because they fail for different reasons and the fix differs. No match
    // means the game never runs a line check from the camera, so there is no distance to
    // be had and the reticle is a direction. Matches with no hits means the ray runs and
    // stops on nothing, which is a flags or range problem. Without both, "the crosshair
    // slides off when I lean" has no evidence behind it.
    void ReportAimTrace() {
        if (!m_traceMatchReported && AimTraceMatchCount() > 0) {
            m_traceMatchReported = true;
            Log::Line("Aim trace: the game's crosshair ray is being seen (%lu matches)",
                      AimTraceMatchCount());
        }
        if (!m_traceHitReported && AimTraceHitCount() > 0) {
            m_traceHitReported = true;
            Log::Line("Aim trace: resolving a distance to the aim point (%lu hits)",
                      AimTraceHitCount());
        }
    }

    // A device created without passing through IDirect3D9::CreateDevice never trips
    // the overlay's hook, and the arming line on its own reads as healthy. Say so
    // once rather than leaving a silently missing marker.
    void ReportOverlayAttach() {
        if (m_overlayReported) {
            return;
        }
        // After the check, not before: the first tick runs before any sleep, so counting
        // there would spend one interval of the deadline on no elapsed time.
        if (IsReticleOverlayAttached()) {
            m_overlayReported = true;
            Log::Line("Aim marker attached to the game's D3D9 device");
        } else if ((m_overlayWaitedMs += kHeartbeatMs) >= kOverlayAttachWaitMs) {
            m_overlayReported = true;
            Log::Line("WARN: the aim marker never attached - the game's D3D9 device "
                      "was created without passing through IDirect3D9::CreateDevice. "
                      "Head tracking still works; the marker will not appear.");
        }
    }

    bool m_firstReport = true;
    bool m_lastReceiving = false;
    bool m_lastDetouring = false;
    bool m_warnedNoRenderCaller = false;
    bool m_warnedNoFovCaller = false;
    bool m_overlayReported = false;
    bool m_crosshairDrawReported = false;
    bool m_crosshairMoveReported = false;
    bool m_crosshairSuppressReported = false;
    bool m_traceMatchReported = false;
    bool m_traceHitReported = false;
    int  m_overlayWaitedMs = 0;
};

void RunHeartbeat() {
    Heartbeat heartbeat;
    do {
        heartbeat.Tick();
    } while (SleepUnlessUnloading(kHeartbeatMs));
}

unsigned __stdcall InitThread(void*) {
    // Before the module wait, not after it: the first question a report has to answer is
    // whether the loader engaged at all, and a mod that waits 30 seconds and gives up
    // leaves no file to answer it with.
    OpenSessionLog();
    Log::Line("%s v%s loaded", kModName, kModVersion);

    if (!g_shutdownEvent) {
        Log::Line("ERROR: shutdown event could not be created; staying dormant");
        return 1;
    }

    if (!WaitForGameModule()) {
        return 1;
    }
    LogFingerprint();

    Config cfg;
    if (!LoadConfig(cfg)) {
        return 1;
    }

    const BuildProfile* profile = MatchRunningProfile();
    if (!profile) {
        // No matching build: stay dormant (no hooks). The game runs vanilla.
        return 0;
    }

    // Armed before the engine-window wait below, because the game creates its D3D9 device
    // while the engine boots and a CreateDevice hook installed after that never fires.
    // This patches d3d9.dll, not the game image, so it is outside the regions the game's
    // startup integrity check covers.
    if (!InstallReticleOverlay(cfg.show_aim_marker)) {
        Log::Line("WARN: the D3D9 overlay is unavailable, so the presented image's size "
                  "is unknown and the game's crosshair will stay at screen centre "
                  "instead of following the aim point");
    }

    if (!StartTrackingAndHotkeys(cfg)) {
        // The overlay patched d3d9.dll before this point. Leaving it in place would keep
        // a detour on the game's CreateDevice for a mod that has given up.
        RemoveReticleOverlay();
        return 1;
    }

    if (!InstallGameHooks(*profile, cfg)) {
        StopTrackingAndHotkeys();
        RemoveReticleOverlay();
        return 1;
    }

    Log::Line("%s ready", kModName);
    RunHeartbeat();
    return 0;
}

// Orderly teardown: unpatch the game, then stop the threads. Only ever reached on a
// FreeLibrary, where the process carries on running and every hook byte and every
// trampoline has to come back out. MinHook suspends process threads to do it, and both
// Stop() calls join a worker, so none of this is safe when the process is already
// exiting - see DllMain.
void ShutdownOrderly() {
    using namespace SpecOpsTheLineHeadTracking;
    RemoveAimTrace();
    RemoveCrosshairHook();
    RemoveCameraHook();
    RemoveReticleOverlay();
    g_hotkeys.Stop();
    g_tracking.Stop();
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            g_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            g_initThreadHandle = reinterpret_cast<HANDLE>(
                _beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr));
            if (!g_initThreadHandle) {
                // The log is opened by that thread, so there is nowhere else to say this.
                OutputDebugStringW(L"SpecOpsTheLineHeadTracking: could not start its init "
                                   L"thread; the mod is doing nothing this session\n");
            }
            break;

        case DLL_PROCESS_DETACH:
            // Unblocks every wait in this file, including the heartbeat, so the init
            // thread stops touching the log and the hooks.
            if (g_shutdownEvent) {
                SetEvent(g_shutdownEvent);
            }

            // lpReserved is non-null when the PROCESS is terminating. Every other thread
            // has already been killed, possibly holding a CRT or heap lock, so joining
            // one (both Stop() calls do) or suspending threads to unpatch (every
            // Remove*Hook does) can hang the game on exit with no way to attribute it.
            // Windows is about to reclaim the address space and the hooked bytes with
            // it, so the correct amount of work here is none.
            if (lpReserved != nullptr) {
                // EmergencyLine, not Line, and no Close. Line takes the log mutex, and
                // the heartbeat thread may have been holding it when Windows killed it -
                // in which case taking it here hangs the game on exit with nothing to
                // attribute it to, which is the failure this whole branch exists to
                // avoid. Core flushes per line, so the file is already complete.
                SpecOpsTheLineHeadTracking::Log::EmergencyLine("%s unloading (process exit)",
                                                               kModName);
                break;
            }

            // FreeLibrary. The process lives on, so everything must come back out - and
            // the init thread must be gone before it does, because it is the thread
            // inside the code being unpatched.
            if (g_initThreadHandle) {
                if (WaitForSingleObject(g_initThreadHandle, 2000) != WAIT_OBJECT_0) {
                    // Every wait in this file is event-driven and unwinds within
                    // kInitPollMs, so this should not happen. Said out loud rather than
                    // discarded, because the teardown below unpatches code that thread
                    // may still be inside.
                    SpecOpsTheLineHeadTracking::Log::Line(
                        "WARN: the init thread did not exit within 2s; unpatching anyway");
                }
                CloseHandle(g_initThreadHandle);
                g_initThreadHandle = nullptr;
            }
            ShutdownOrderly();
            // The event outlives the wait deliberately. A thread that did not exit in
            // time is still inside SleepUnlessUnloading, and Win32 recycles handle
            // values: closing it here hands that thread a handle the process may have
            // reissued for something else. One leaked handle is the cheaper end of that
            // trade.
            SpecOpsTheLineHeadTracking::Log::Line("%s unloading", kModName);
            SpecOpsTheLineHeadTracking::Log::Close();
            break;
    }
    return TRUE;
}
