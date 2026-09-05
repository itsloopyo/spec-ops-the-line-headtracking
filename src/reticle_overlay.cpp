#include "reticle_overlay.h"

#include "aim_marker.h"
#include "aim_projection.h"
#include "logging.h"

#include "minhook_util.h"

#include <windows.h>
#include <d3d9.h>

#include <atomic>
#include <cmath>

namespace SpecOpsTheLineHeadTracking {

namespace {

using Direct3DCreate9_t = IDirect3D9* (WINAPI*)(UINT);
using CreateDevice_t = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                                    DWORD, D3DPRESENT_PARAMETERS*,
                                                    IDirect3DDevice9**);
using EndScene_t = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);

std::atomic<CreateDevice_t> g_oCreateDevice{nullptr};
// Written by the init thread, read on the render thread. Atomic so the single-read
// argument the teardown rests on is one the type actually gives: a plain global may be
// reloaded at the call site whatever the local looks like.
std::atomic<EndScene_t> g_oEndScene{nullptr};
void*          g_pCreateDevice = nullptr;
void*          g_pEndScene     = nullptr;

// Re-entrancy latch for InstallDeviceHooks. Separate from g_overlayAttached: this is
// claimed before the hook goes in, so it cannot also answer "is the marker attached".
volatile LONG g_deviceHookAttempted = 0;
std::atomic<bool> g_overlayAttached{false};
std::atomic<bool> g_drawMarker{false};

// COM vtable slots. Both interfaces are frozen by the D3D9 ABI, so the indices are
// stable for every d3d9.dll the game can load.
constexpr int kD3D9CreateDeviceSlot = 16;
constexpr int kDeviceEndSceneSlot = 42;

struct Vertex {
    float x, y, z, rhw;
    D3DCOLOR color;
};
constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

// Marker geometry in pixels at 1080p, scaled by the presented image's height so the
// reticle is the same apparent size at any resolution.
constexpr float kRefHeight = 1080.0f;
constexpr float kGap  = 5.0f;
constexpr float kTick = 7.0f;
constexpr float kDot  = 2.0f;
constexpr int kQuads = 5;
constexpr int kVerts = kQuads * 6;
constexpr D3DCOLOR kMarkerColor = D3DCOLOR_ARGB(255, 255, 255, 255);
constexpr D3DCOLOR kOutlineColor = D3DCOLOR_ARGB(190, 0, 0, 0);
// How far the black pass is grown past the white one, in pixels.
constexpr float kOutlineBloat = 1.0f;

// D3D9 samples a pre-transformed vertex at the pixel CENTRE, so a screen-space quad
// has to sit half a pixel back or it covers the wrong pixels: the black outline comes
// out one pixel on one side and two on the other, and a quad narrower than a pixel can
// straddle a boundary and enclose no centre at all, which made the marker's dot vanish
// below 1080p.
constexpr float kHalfPixel = 0.5f;
constexpr DWORD kWriteAllChannels = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                                    D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA;
// Never let a half-extent shrink below this, so every part of the marker always
// encloses at least one pixel centre.
constexpr float kMinHalfExtent = 0.6f;

float AtLeast(float v) {
    return v < kMinHalfExtent ? kMinHalfExtent : v;
}

void PushQuad(Vertex* v, float cx, float cy, float halfW, float halfH, D3DCOLOR c) {
    halfW = AtLeast(halfW);
    halfH = AtLeast(halfH);
    const float l = cx - halfW - kHalfPixel, r = cx + halfW - kHalfPixel;
    const float t = cy - halfH - kHalfPixel, b = cy + halfH - kHalfPixel;
    v[0] = { l, t, 0.0f, 1.0f, c };
    v[1] = { r, t, 0.0f, 1.0f, c };
    v[2] = { l, b, 0.0f, 1.0f, c };
    v[3] = { r, t, 0.0f, 1.0f, c };
    v[4] = { r, b, 0.0f, 1.0f, c };
    v[5] = { l, b, 0.0f, 1.0f, c };
}

// One marker is five axis-aligned quads: a centre dot and four ticks. Drawing it twice,
// black one pixel out then white, keeps it readable against both the Dubai sand and the
// dark interiors without needing a texture.
void BuildMarker(Vertex (&out)[kVerts], float cx, float cy, float s, D3DCOLOR c,
                 float bloat) {
    const float gap = kGap * s, tick = kTick * s;
    const float w = 0.5f * s + bloat;
    const float dot = kDot * s * 0.5f + bloat;
    int n = 0;
    PushQuad(out + n, cx, cy, dot, dot, c); n += 6;
    PushQuad(out + n, cx - gap - tick * 0.5f, cy, tick * 0.5f + bloat, w, c); n += 6;
    PushQuad(out + n, cx + gap + tick * 0.5f, cy, tick * 0.5f + bloat, w, c); n += 6;
    PushQuad(out + n, cx, cy - gap - tick * 0.5f, w, tick * 0.5f + bloat, c); n += 6;
    PushQuad(out + n, cx, cy + gap + tick * 0.5f, w, tick * 0.5f + bloat, c); n += 6;
    static_assert(kVerts == 30, "BuildMarker writes exactly kQuads * 6 vertices");
}

// Resolves the surface the frame is being presented from, and reports its size. It
// doubles as the "are we drawing into the real backbuffer" guard: UE3 renders offscreen
// passes and a marker drawn into one of those would be baked into a shadow or
// post-process target. The size comes from the surface rather than from GetViewport
// because the viewport is whatever the last pass left bound, which is not necessarily
// the presented image.
bool BackBufferSize(IDirect3DDevice9* device, UINT* width, UINT* height) {
    IDirect3DSurface9* target = nullptr;
    if (FAILED(device->GetRenderTarget(0, &target)) || target == nullptr) {
        return false;
    }
    IDirect3DSurface9* back = nullptr;
    bool ok = false;
    if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) &&
        back != nullptr) {
        if (back == target) {
            D3DSURFACE_DESC desc{};
            if (SUCCEEDED(back->GetDesc(&desc)) && desc.Width > 0 && desc.Height > 0) {
                *width = desc.Width;
                *height = desc.Height;
                ok = true;
            }
        }
        back->Release();
    }
    target->Release();
    return ok;
}

// Every term the marker depends on, on ONE line, written when the state it describes
// changes. Reading the position from one log line and the field of view from another is
// how three fixes get shipped against the wrong fault: each half-reading fits several
// faults equally well.
//
// Bail reasons are counted rather than logged individually because they happen per
// frame. A marker that never appears is then one line to diagnose: "endScene" says the
// hook runs, "active" says the camera hook is publishing, "backbuf" says the guard
// accepts the pass, "drawn" says the geometry was submitted, and the ndc says where.
struct DrawStats {
    unsigned endScene, active, backbuf, behind, badFov, offScreen, noBlock, drawn;
    unsigned notFinite;
    float ndcX, ndcY, cx, cy, right, up, fwd, fov, dist;
    float leanR, leanU, leanF;
    float viewX, viewY, viewW, viewH;
    UINT w, h;
};
DrawStats g_stats = {};

// Which stage of the draw the last second ended in. Counted over a window rather than
// read per frame: UE3 calls EndScene several times a frame and only one of those passes
// is the presented image, so a single frame's answer is whichever pass ran last.
enum class MarkerState {
    Unknown,
    Drawing,
    Inactive,
    NotPresentPass,
    NoStateBlock,
    BehindCamera,
    BadFov,
    NotFinite,
    ProjectedOffFrame,
    NoAimPoint,
};

const char* Describe(MarkerState state) {
    switch (state) {
        case MarkerState::Drawing:           return "drawing";
        case MarkerState::Inactive:          return "idle (camera hook publishing nothing)";
        case MarkerState::NotPresentPass:    return "no presented frame reached the hook";
        case MarkerState::NoStateBlock:      return "the device refused a state block";
        case MarkerState::BehindCamera:      return "the aim point is behind the camera";
        case MarkerState::BadFov:            return "no usable field of view";
        case MarkerState::NotFinite:         return "the projection produced a non-finite position";
        case MarkerState::ProjectedOffFrame: return "projected off the frame";
        case MarkerState::NoAimPoint:        return "no aim point published";
        case MarkerState::Unknown:           break;
    }
    return "unknown";
}

// Ordered by how early the draw gave up, so the reported state names the first stage
// that stopped it rather than a later one it never reached.
MarkerState ClassifyWindow() {
    if (g_stats.drawn > 0)     return MarkerState::Drawing;
    if (g_stats.active == 0)   return MarkerState::Inactive;
    if (g_stats.backbuf == 0)  return MarkerState::NotPresentPass;
    if (g_stats.noBlock > 0)   return MarkerState::NoStateBlock;
    if (g_stats.behind > 0)    return MarkerState::BehindCamera;
    if (g_stats.badFov > 0)    return MarkerState::BadFov;
    if (g_stats.notFinite > 0) return MarkerState::NotFinite;
    if (g_stats.offScreen > 0) return MarkerState::ProjectedOffFrame;
    return MarkerState::NoAimPoint;
}

// A second of frames per window, and a state has to hold for two of them before it is
// written: aiming past the edge of the frame flips drawing to off-frame and back as a
// normal part of playing, and a line every time that happens is the per-second dump
// again under another name.
constexpr int kStateWindows = 2;

void ReportStats() {
    static DWORD s_last = 0;
    const DWORD now = GetTickCount();
    if (s_last == 0) {
        s_last = now;
        return;
    }
    if (now - s_last < 1000) {
        return;
    }
    s_last = now;

    static MarkerState s_reported = MarkerState::Unknown;
    static MarkerState s_pending = MarkerState::Unknown;
    static int s_pendingWindows = 0;

    const MarkerState state = ClassifyWindow();
    if (state != s_pending) {
        s_pending = state;
        s_pendingWindows = 0;
    }
    ++s_pendingWindows;

    if (state != s_reported && s_pendingWindows >= kStateWindows) {
        s_reported = state;
        Log::Line("RETICLE %s | endScene=%u active=%u backbuf=%u drawn=%u "
                  "(behind=%u badfov=%u notfinite=%u offscreen=%u noblock=%u) | fov=%.1f rt=%ux%u "
                  "view=%.0fx%.0f+%.0f+%.0f "
                  "aim(r,u,f)=(%.3f,%.3f,%.3f) dist=%.0f lean(r,u,f)cm=(%.1f,%.1f,%.1f) "
                  "ndc=(%.3f,%.3f) px=(%.0f,%.0f)",
                  Describe(state),
                  g_stats.endScene, g_stats.active, g_stats.backbuf, g_stats.drawn,
                  g_stats.behind, g_stats.badFov, g_stats.notFinite, g_stats.offScreen,
                  g_stats.noBlock,
                  g_stats.fov, g_stats.w, g_stats.h,
                  g_stats.viewW, g_stats.viewH, g_stats.viewX, g_stats.viewY,
                  g_stats.right, g_stats.up, g_stats.fwd, g_stats.dist,
                  g_stats.leanR, g_stats.leanU, g_stats.leanF,
                  g_stats.ndcX, g_stats.ndcY, g_stats.cx, g_stats.cy);
    }

    g_stats.endScene = g_stats.active = g_stats.backbuf = g_stats.drawn = 0;
    g_stats.behind = g_stats.badFov = g_stats.offScreen = g_stats.noBlock = 0;
    g_stats.notFinite = 0;
    // Cleared with the counters, not left standing. These are only written on a frame
    // that projected successfully, so a window classified "behind camera" would
    // otherwise print the pixel position of whichever earlier frame did - a stale term
    // under a fresh heading, which is the exact reading error this line exists to stop.
    g_stats.ndcX = g_stats.ndcY = g_stats.cx = g_stats.cy = 0.0f;
}

// Every state the marker draw depends on, set explicitly rather than inherited. The
// marker is submitted into whatever the game left bound at EndScene, and one leftover
// shader, texture stage or stream frequency is enough for it to come out the wrong
// colour, smeared with a leftover texture, or not at all. The caller brackets this with
// a state block, so the game gets its own settings back before it presents.
void ApplyMarkerRenderState(IDirect3DDevice9* device, UINT width, UINT height) {
    // Pre-transformed vertices are clipped against the BOUND VIEWPORT, which is whatever
    // the last pass left set - the same reason BackBufferSize refuses to take the size
    // from it. A viewport left over from a downsampled post-process step clips the
    // marker away while g_stats.drawn still reports it submitted.
    const D3DVIEWPORT9 viewport = { 0, 0, width, height, 0.0f, 1.0f };
    device->SetViewport(&viewport);
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetVertexDeclaration(nullptr);
    device->SetFVF(kFvf);
    device->SetTexture(0, nullptr);
    // UE3's D3D9 path draws foliage and decals with hardware instancing, which leaves a
    // stream frequency set; DrawPrimitiveUP against that renders nothing.
    device->SetStreamSourceFreq(0, 1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPING, TRUE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, kWriteAllChannels);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    // Without this a stage the game left enabled cascades over ours.
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

void DrawMarker(IDirect3DDevice9* device, UINT width, UINT height) {
    AimMarker& marker = GetAimMarker();

    // Everything the projection depends on is read and recorded BEFORE any of the bails
    // below, so the reported line describes a frame that failed rather than the last one
    // that succeeded. A stale term fits several faults equally well, which is
    // how a fix gets shipped against the wrong one.
    const float right = marker.right.load(std::memory_order_relaxed);
    const float up = marker.up.load(std::memory_order_relaxed);
    const float fwd = marker.forward.load(std::memory_order_relaxed);
    const float fov = marker.fov_deg.load(std::memory_order_relaxed);
    g_stats.right = right;
    g_stats.up = up;
    g_stats.fwd = fwd;
    g_stats.fov = fov;
    g_stats.dist = marker.distance.load(std::memory_order_relaxed);
    g_stats.leanR = marker.lean_right.load(std::memory_order_relaxed);
    g_stats.leanU = marker.lean_up.load(std::memory_order_relaxed);
    g_stats.leanF = marker.lean_forward.load(std::memory_order_relaxed);
    g_stats.w = width;
    g_stats.h = height;

    // The game letterboxes the world into a fixed-aspect band inside the window, so the
    // projection's coordinates span that band and not the backbuffer.
    const ViewRect view = ViewRectFor(static_cast<float>(width), static_cast<float>(height));
    g_stats.viewX = view.x;
    g_stats.viewY = view.y;
    g_stats.viewW = view.width;
    g_stats.viewH = view.height;

    float ndcX = 0.0f, ndcY = 0.0f;
    switch (ProjectAim(view.width / view.height, &ndcX, &ndcY)) {
        case AimProjection::Ok:
            break;
        case AimProjection::Behind:
            ++g_stats.behind;
            return;
        case AimProjection::BadFov:
            ++g_stats.badFov;
            return;
        case AimProjection::NotFinite:
            ++g_stats.notFinite;
            return;
        case AimProjection::Inactive:
            return;
    }

    const float cx = view.x + (0.5f + 0.5f * ndcX) * view.width;
    const float cy = view.y + (0.5f - 0.5f * ndcY) * view.height;
    const float scale = view.height / kRefHeight;
    g_stats.ndcX = ndcX;
    g_stats.ndcY = ndcY;
    g_stats.cx = cx;
    g_stats.cy = cy;

    // Off the frame entirely: hide rather than clamp to the edge, because a marker
    // parked on the edge claims the shot lands there when it does not. Submitting the
    // geometry anyway would put vertices thousands of pixels outside the guard band on
    // the way to being clipped to nothing.
    const float margin = (kGap + kTick) * scale + 2.0f;
    if (cx < view.x - margin || cy < view.y - margin ||
        cx > view.x + view.width + margin ||
        cy > view.y + view.height + margin) {
        ++g_stats.offScreen;
        return;
    }

    // Created and released within the frame rather than kept alive. A state block held
    // across frames belongs to one device, and Reset fails outright while one is
    // outstanding: keeping it meant a device recreation left the new device permanently
    // configured with this marker's render states, and a mode change could wedge the
    // game on a black screen. Nothing here outlives the call.
    UINT savedFreq = 1;
    if (FAILED(device->GetStreamSourceFreq(0, &savedFreq))) {
        savedFreq = 1;
    }

    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)) || saved == nullptr) {
        // D3D9 documents state-block creation as invalid between BeginScene and
        // EndScene, which is exactly where this runs. The retail runtime tolerates it,
        // the debug one need not, and a silent return here is a marker that never
        // appears with nothing in the log to say why.
        ++g_stats.noBlock;
        return;
    }

    ApplyMarkerRenderState(device, width, height);

    Vertex verts[kVerts];
    BuildMarker(verts, cx, cy, scale, kOutlineColor, kOutlineBloat);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, kVerts / 3, verts, sizeof(Vertex));
    BuildMarker(verts, cx, cy, scale, kMarkerColor, 0.0f);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, kVerts / 3, verts, sizeof(Vertex));

    saved->Apply();
    saved->Release();
    // Restored by hand rather than trusted to the state block. The stream frequency is
    // the one piece of state here the game is known to leave non-default (UE3 instances
    // foliage and decals through it), and whether D3DSBT_ALL captures it is not
    // something the D3D9 contract is explicit about. Getting it wrong renders the
    // game's grass wrong for the session with no other symptom.
    device->SetStreamSourceFreq(0, savedFreq);
    ++g_stats.drawn;
}

HRESULT STDMETHODCALLTYPE HkEndScene(IDirect3DDevice9* device) {
    // The cheap atomic first: UE3 calls EndScene for passes other than the presented
    // frame, and with tracking off there is nothing to draw, so neither the surface
    // queries nor the state block should run at all.
    // Only counted when something resets them. ReportStats is the sole reset and it is
    // gated on the marker being drawn, so counting regardless would leave three unsigned
    // counters climbing for the session in the default configuration.
    const bool drawMarker = g_drawMarker.load(std::memory_order_relaxed);
    if (drawMarker) {
        ++g_stats.endScene;
    }
    AimMarker& marker = GetAimMarker();
    if (marker.active.load(std::memory_order_acquire)) {
        if (drawMarker) {
            ++g_stats.active;
        }
        UINT w = 0, h = 0;
        if (BackBufferSize(device, &w, &h)) {
            if (drawMarker) {
                ++g_stats.backbuf;
            }
            // Published whether or not the marker is drawn: this hook is the only part of
            // the mod that sees the presented image, and the crosshair hook needs its
            // size to turn the projection into the pixels the HUD draws in.
            marker.render_width.store(static_cast<float>(w), std::memory_order_relaxed);
            marker.render_height.store(static_cast<float>(h), std::memory_order_relaxed);
            if (drawMarker) {
                DrawMarker(device, w, h);
            }
        }
    }
    if (drawMarker) {
        ReportStats();
    }
    // Read once into a local. RemoveReticleOverlay nulls this before it unpatches, so a
    // render thread that is inside this detour while the DLL unloads sees either a
    // trampoline that is still mapped or a null it can bail on - never the freed
    // trampoline MH_RemoveHook leaves behind. Dropping one EndScene during teardown
    // costs a frame; calling through a null or a freed trampoline kills the process.
    const EndScene_t original = g_oEndScene.load(std::memory_order_acquire);
    if (!original) {
        // The scene was NOT ended, so saying D3D_OK would be a lie the caller acts on:
        // the device's scene stays open and every draw in the game's next frame fails
        // against a BeginScene that returns D3DERR_INVALIDCALL, with nothing to explain
        // it. Only reachable while the DLL is unloading.
        return D3DERR_INVALIDCALL;
    }
    return original(device);
}

void InstallDeviceHooks(IDirect3DDevice9* device) {
    if (InterlockedCompareExchange(&g_deviceHookAttempted, 1, 0) != 0) {
        return;
    }

    void** vtbl = *reinterpret_cast<void***>(device);
    void* pEndScene = vtbl[kDeviceEndSceneSlot];

    EndScene_t original = nullptr;
    const MH_STATUS st = CreateAndEnableHook(pEndScene, reinterpret_cast<void*>(&HkEndScene),
                                             reinterpret_cast<void**>(&original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking EndScene failed: %d; no aim marker this session", st);
        MH_RemoveHook(pEndScene);
        InterlockedExchange(&g_deviceHookAttempted, 0);
        return;
    }
    g_oEndScene.store(original, std::memory_order_release);
    g_pEndScene = pEndScene;
    // Set only once the detour can actually run, so the heartbeat cannot report the
    // marker attached to a device whose EndScene is not hooked.
    g_overlayAttached.store(true, std::memory_order_release);
    Log::Line("Reticle overlay attached to the game device (EndScene=%p)", pEndScene);
}

HRESULT STDMETHODCALLTYPE HkCreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type,
                                         HWND focusWindow, DWORD behaviorFlags,
                                         D3DPRESENT_PARAMETERS* pp,
                                         IDirect3DDevice9** returnedDevice) {
    // Same one-shot read as HkEndScene: the trampoline is nulled before it is freed.
    const CreateDevice_t original = g_oCreateDevice.load(std::memory_order_acquire);
    if (!original) {
        return D3DERR_INVALIDCALL;
    }
    HRESULT hr = original(self, adapter, type, focusWindow, behaviorFlags, pp,
                          returnedDevice);
    if (SUCCEEDED(hr) && returnedDevice != nullptr && *returnedDevice != nullptr) {
        InstallDeviceHooks(*returnedDevice);
    }
    return hr;
}

// Reads IDirect3D9::CreateDevice from a throwaway D3D9 object. Creating an IDirect3D9
// allocates no device, no window and no swap chain, so it cannot conflict with the
// game's own device the way a dummy IDirect3DDevice9 would. The vtable is shared by
// every IDirect3D9 from the same d3d9.dll, so hooking this address catches the game's
// call.
void* GetCreateDeviceAddress() {
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) {
        d3d9 = LoadLibraryA("d3d9.dll");
    }
    if (!d3d9) {
        Log::Line("ERROR: d3d9.dll not loaded");
        return nullptr;
    }
    auto pCreate = reinterpret_cast<Direct3DCreate9_t>(
        GetProcAddress(d3d9, "Direct3DCreate9"));
    if (!pCreate) {
        Log::Line("ERROR: Direct3DCreate9 export not found");
        return nullptr;
    }
    IDirect3D9* d3d = pCreate(D3D_SDK_VERSION);
    if (!d3d) {
        Log::Line("ERROR: Direct3DCreate9 returned null");
        return nullptr;
    }
    void** vtbl = *reinterpret_cast<void***>(d3d);
    void* pCreateDevice = vtbl[kD3D9CreateDeviceSlot];
    d3d->Release();
    return pCreateDevice;
}

}  // namespace

bool IsReticleOverlayAttached() {
    return g_overlayAttached.load(std::memory_order_acquire);
}

bool InstallReticleOverlay(bool drawAimMarker) {
    g_drawMarker.store(drawAimMarker, std::memory_order_relaxed);
    // MinHook is already initialised by the camera hook; only a fresh process reaches
    // the uninitialised case.
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        Log::Line("ERROR: MH_Initialize failed for the reticle overlay");
        return false;
    }

    void* pCreateDevice = GetCreateDeviceAddress();
    if (!pCreateDevice) {
        return false;
    }
    CreateDevice_t original = nullptr;
    const MH_STATUS st = CreateAndEnableHook(pCreateDevice,
                                             reinterpret_cast<void*>(&HkCreateDevice),
                                             reinterpret_cast<void**>(&original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking IDirect3D9::CreateDevice failed: %d", st);
        MH_RemoveHook(pCreateDevice);
        return false;
    }
    g_oCreateDevice.store(original, std::memory_order_release);
    g_pCreateDevice = pCreateDevice;
    Log::Line("Reticle overlay armed; waiting for the game to create its device");
    return true;
}

void RemoveReticleOverlay() {
    // Null the trampolines FIRST, then unpatch, then free. MH_RemoveHook releases the
    // trampoline buffer, so the order matters: a render thread inside a detour reads the
    // pointer once, and it must see either a trampoline that is still mapped or a null.
    // Nulling after the free - which is what this used to do - guaranteed that an
    // in-flight EndScene either called freed executable memory or called through a null.
    g_oEndScene.store(nullptr, std::memory_order_release);
    g_oCreateDevice.store(nullptr, std::memory_order_release);
    g_overlayAttached.store(false, std::memory_order_release);
    if (g_pEndScene) {
        MH_DisableHook(g_pEndScene);
        MH_RemoveHook(g_pEndScene);
        g_pEndScene = nullptr;
    }
    if (g_pCreateDevice) {
        MH_DisableHook(g_pCreateDevice);
        MH_RemoveHook(g_pCreateDevice);
        g_pCreateDevice = nullptr;
    }
    InterlockedExchange(&g_deviceHookAttempted, 0);
}

}  // namespace SpecOpsTheLineHeadTracking
