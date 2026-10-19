// Engine patches for CloakNTEngine.dll (demo build): display mode whitelist, real aspect ratio, Hor+ FOV,
// unstretched UI.
//
// How the engine handles the screen (see README "How it works"):
//  - Renderer::Init keeps only enumerated modes whose width, height and refresh are in fixed tables
//    (widths up to 2560), so 3440x1440 is never offered and never runs.
//  - Renderer::SetViewport computes h/w but stores 0.5625 (16:9) for anything wider than 3:2, else 0.75.
//    Gui::SetPhysicalViewport rounds the same way, so menus and HUD are stretched on wider screens.
//  - The FOV is a horizontal angle on a 4:3 basis. Camera::UpdateCachedData (projection),
//    EffectManager::advanceTimeFov ("real fov") and ViewTrigger::IsInActiveRange (view cone) widen it for
//    "widescreen" with a fixed 16:9 / 4:3 factor. The grass extents divide by 4/3 and the aspect value.
//  - The deferred ambient passes shade through a fixed view-space quad (+-7000 at depth 3500), which only
//    covers 2 * atan(2) = 126.9 degrees: wider views get unshaded (white) edges.
#include "dh2013ws.h"
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace {

// ---- CloakNTEngine.dll RVAs (preferred base 0x10000000) -----------------------------------------
constexpr uintptr_t kRendererInit = 0x256780;   // bool Renderer::Init(uint, bool, uint w, uint h, uint hz, bool, bool)
constexpr uintptr_t kModeFilter = 0x256885;     // Init: whitelist test of the mode just enumerated
constexpr uintptr_t kModeAccept = 0x2568a3;     //   -> count++
constexpr uintptr_t kModeReject = 0x2568b0;     //   -> next mode
constexpr uintptr_t kWidthTable = 0x6b13f8, kHeightTable = 0x6b1428, kRefreshTable = 0x6b146c;
constexpr uintptr_t kSetViewport = 0x255140;    // void Renderer::SetViewport(uint x, uint y, uint w, uint h)
constexpr uintptr_t kCameraFov = 0x31056d;      // Camera::UpdateCachedData: widen fov, load aspect
constexpr uintptr_t kCameraFovEnd = 0x3105cc;   //   -> mtProjection(fov=[ebp-4], aspect=[ebp-0xc], ...)
constexpr uintptr_t kEffectFov = 0x1eaefe;      // EffectManager::advanceTimeFov: widen fov -> xmm0
constexpr uintptr_t kEffectFovEnd = 0x1eaf48;   //   -> movss [edi+0xa8], xmm0
constexpr uintptr_t kTriggerFov = 0x395eee;     // ViewTrigger::IsInActiveRange: widen [ebp+8]
constexpr uintptr_t kTriggerFovEnd = 0x395f33;
constexpr uintptr_t kGrass43[4] = {0x22b1e4, 0x22b8ac, 0x22c969, 0x22e9de};  // fdiv qword ptr [4/3]
constexpr uintptr_t kConst43 = 0x6af018;        // double 4/3
constexpr uintptr_t kDefaultFov = 0x8280ac;     // float: default player FOV (radians)
// RenderShadeAmbient / RenderShadeAmbientDirectional: movss xmm0, [-7000] and movss xmm1, [7000]
constexpr uintptr_t kQuadNeg[2] = {0x22fb43, 0x2303d3}, kQuadPos[2] = {0x22fb4b, 0x2303db};
constexpr uintptr_t kQuadNegConst = 0x6b0d70, kQuadPosConst = 0x6b0d6c;
constexpr uintptr_t kGuiAspect = 0x25c60d;      // Gui::SetPhysicalViewport: movss xmm0, [0.5625] (wide screens)
constexpr uintptr_t kGuiAspectEnd = 0x25c61f;
constexpr uintptr_t kConst169 = 0x6b1b5c;       // float 0.5625
constexpr uintptr_t kCrosshairMul = 0x2dc44e;   // PlayerHud::Crosshair_AdvanceTime: fmul qword ptr [42.0]
constexpr uintptr_t kConstCrosshair = 0x6b8590;
constexpr uintptr_t kScopeDiv = 0x3bf4ff;       // wsModeHudBase::RenderHUD: fdiv qword ptr [1280.0] (wide)
constexpr uintptr_t kConstScopeDiv = 0x6c73e8;
constexpr uintptr_t kDropAspect = 0x3ba871;     // wsModeAimWeapon::ChangeDropMarkers: fld [eax+0x1409c]
constexpr uintptr_t kDropAspectEnd = 0x3ba877;

// Renderer fields
constexpr size_t kModeCount = 0x12148;  // uint
constexpr size_t kModes = 0x1214c;      // D3DDISPLAYMODE[256] (Width, Height, RefreshRate, Format)
constexpr uint32_t kModeSlots = 256;
constexpr size_t kAspect = 0x1409c;     // float h/w used by the 3D view (snapped)
constexpr size_t kAspectFull = 0x140a0; // float h/w of the full-screen viewport (snapped)
constexpr size_t kWide = 0x140ac;       // bool: h/w < 0.666
constexpr size_t kAspectGui = 0x140b0;  // float h/w used for GUI text and GUI 3D views (snapped)
// Camera fields
constexpr size_t kCamFov = 0xf8;        // float, radians

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kMaxFov = 170.0 * kDeg;

uintptr_t At(uintptr_t rva) { return g_engine + rva; }

// Builds expected code bytes; `rvas` lists absolute addresses (relocated with the module) at byte offsets.
struct Expect {
    uint8_t b[64];
    size_t n = 0;
    Expect& Bytes(std::initializer_list<uint8_t> list) {
        for (uint8_t x : list) b[n++] = x;
        return *this;
    }
    Expect& Addr(uintptr_t rva) {
        uint32_t v = static_cast<uint32_t>(At(rva));
        std::memcpy(b + n, &v, 4);
        n += 4;
        return *this;
    }
};

// ---- field of view --------------------------------------------------------------------------
double g_refFov = 60.0 * kDeg;  // game default FOV (read from the engine at install)
double g_k = 1.0;               // tan(user/2) / tan(ref/2): widening of the normal view
double g_basis43 = 4.0 / 3.0;   // replaces the 4/3 constant in the grass extents: (4/3) / k
double g_crosshairMul = 42.0;   // replaces 42.0 in the crosshair scale: 42 / widening of the current view
double g_scopeDiv = 1280.0;     // replaces 1280 (16:9 canvas width) in the scope overlay size: 720 * w / h
// Ambient shading quad: +-50000 at depth 3500 covers 2 * atan(50000 / 3500) = 172 degrees (FOV clamp is 170).
float g_quadNeg = -50000.0f, g_quadPos = 50000.0f;
float g_dropAspect = 0.5625f;   // bullet-drop markers keep the game's 16:9 maths (the vertical view is unchanged)

void SetUserFov(float deg) {
    g_cfg.fov = deg;
    g_k = deg > 0.0f ? std::tan(deg * kDeg * 0.5) / std::tan(g_refFov * 0.5) : 1.0;
    g_basis43 = (4.0 / 3.0) / g_k;
}

// Widening for a camera FOV `f`: full for the normal view and anything wider, fading to none as the view
// zooms in, so scopes and binoculars keep their magnification.
double FovScale(double f) {
    if (g_k == 1.0) return 1.0;
    double z = std::tan(g_refFov * 0.5) / std::tan(f * 0.5);  // zoom relative to the default view
    if (z <= 1.0) return g_k;
    if (g_cfg.zoomBlend <= 1.0f) return 1.0;
    double w = (z - 1.0) / (g_cfg.zoomBlend - 1.0);
    return w >= 1.0 ? 1.0 : std::pow(g_k, 1.0 - w);
}

// The horizontal FOV the engine should render for camera FOV `f` (4:3 basis). Same as the game's
// formula, but with the real aspect ratio instead of 16:9 and the user's FOV on top.
float RealFov(float f, const uint8_t* r) {
    if (!(f > 0.0f && f < kPi)) return f;
    double t = std::tan(f * 0.5);
    if (r && r[kWide]) {
        float aspect = *reinterpret_cast<const float*>(r + kAspect);  // h/w
        if (aspect > 0.0f) t *= (1.0 / aspect) / (4.0 / 3.0);
    }
    t *= FovScale(f);
    double out = 2.0 * std::atan(t);
    return static_cast<float>(out > kMaxFov ? kMaxFov : out);
}

// FOV hotkeys, polled once per frame from the player's FOV update while the game window has focus.
void PollFovKeys() {
    static bool down[3];
    const int keys[3] = {g_cfg.keyFovUp, g_cfg.keyFovDown, g_cfg.keyFovReset};
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    bool focus = pid == GetCurrentProcessId();
    for (int i = 0; i < 3; i++) {
        bool now = focus && keys[i] > 0 && (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        bool pressed = now && !down[i];
        down[i] = now;
        if (!pressed) continue;
        float cur = g_cfg.fov > 0.0f ? g_cfg.fov : static_cast<float>(g_refFov / kDeg);
        if (i == 0) cur += g_cfg.fovStep;
        if (i == 1) cur -= g_cfg.fovStep;
        if (cur < 20.0f) cur = 20.0f;
        if (cur > 150.0f) cur = 150.0f;
        SetUserFov(i == 2 ? 0.0f : cur);
        SaveFovToIni();
        Log("FOV %s: %.1f degrees (4:3 basis)", i == 2 ? "reset" : "set", i == 2 ? g_refFov / kDeg : cur);
    }
}

// ---- called from the patched engine code ----------------------------------------------------
void __stdcall CameraFov(const uint8_t* cam, const uint8_t* r, float* fovOut, float* aspectOut) {
    *fovOut = RealFov(*reinterpret_cast<const float*>(cam + kCamFov), r);
    *aspectOut = *reinterpret_cast<const float*>(r + kAspect);
}

float __stdcall EffectFov(float f, const uint8_t* r) {
    PollFovKeys();
    // Log what the default (unzoomed) view renders at whenever that changes.
    static float lastNormal;
    float normal = RealFov(static_cast<float>(g_refFov), r);
    if (std::fabs(normal - lastNormal) > 0.001f) {
        lastNormal = normal;
        Log("default view renders %.1f degrees horizontal (aspect h/w %.4f)", normal / kDeg,
            r ? *reinterpret_cast<const float*>(r + kAspect) : 0.0f);
    }
    // The crosshair is sized for the game's FOV; with a wider view the same spread covers less of the screen.
    g_crosshairMul = g_cfg.crosshairFov ? 42.0 / FovScale(f) : 42.0;
    return RealFov(f, r);
}

float __stdcall TriggerFov(float f, const uint8_t* r) { return RealFov(f, r); }

bool __stdcall AcceptMode(const uint8_t* r) {
    uint32_t count = *reinterpret_cast<const uint32_t*>(r + kModeCount);
    const uint32_t* m = reinterpret_cast<const uint32_t*>(r + kModes + count * 16);
    uint32_t w = m[0], h = m[1], hz = m[2];
    // The next mode is enumerated into slot `count`, so keep one slot free.
    if (count + 2 > kModeSlots) return false;
    // The engine sorts modes by ((w << 12 | h) << 8 | hz): keep the fields inside their bits.
    if (w < 800 || h < 600 || w > 4095 || h > 4095) return false;
    if (hz < static_cast<uint32_t>(g_cfg.minRefresh) || hz > 255) return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t* o = reinterpret_cast<const uint32_t*>(r + kModes + i * 16);
        if (o[0] == w && o[1] == h && o[2] == hz) return false;
    }
    Log("mode %ux%u@%u listed", w, h, hz);
    return true;
}

// ---- hooks ------------------------------------------------------------------------------------
using SetViewportFn = void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
SetViewportFn g_origSetViewport;

void __fastcall SetViewportHook(uint8_t* r, void*, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    g_origSetViewport(r, x, y, w, h);
    if (!w || !h) return;
    float real = static_cast<float>(h) / static_cast<float>(w);
    if (real < 0.666f) {  // the game stores 16:9 for anything wider than 3:2
        if (g_cfg.fixAspect) {
            *reinterpret_cast<float*>(r + kAspect) = real;
            *reinterpret_cast<float*>(r + kAspectFull) = real;
        }
        if (g_cfg.fixUI) {
            *reinterpret_cast<float*>(r + kAspectGui) = real;
            g_scopeDiv = 720.0 * w / h;
        }
    }
    static uint32_t lastW, lastH;
    if (w != lastW || h != lastH) {
        lastW = w;
        lastH = h;
        Log("viewport %ux%u at %u,%u: 3D aspect h/w %.4f, UI h/w %.4f", w, h, x, y,
            *reinterpret_cast<float*>(r + kAspect), *reinterpret_cast<float*>(r + kAspectGui));
    }
}

using InitFn = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
InitFn g_origInit;

// Highest refresh rate the primary display offers for w x h (0 if none).
uint32_t BestRefresh(uint32_t w, uint32_t h) {
    DEVMODEA dm = {};
    dm.dmSize = sizeof dm;
    uint32_t best = 0;
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); i++) {
        if (dm.dmPelsWidth == w && dm.dmPelsHeight == h && dm.dmDisplayFrequency <= 255 &&
            dm.dmDisplayFrequency > best)
            best = dm.dmDisplayFrequency;
    }
    return best;
}

uint32_t __fastcall InitHook(void* r, void*, uint32_t adapter, uint32_t b, uint32_t w, uint32_t h, uint32_t hz,
                             uint32_t c, uint32_t d) {
    Log("Renderer::Init: setting %ux%u@%u", w, h, hz);  // 666x666@66 = no setting yet (desktop mode)
    if (g_cfg.width > 0 && g_cfg.height > 0) {
        w = static_cast<uint32_t>(g_cfg.width);
        h = static_cast<uint32_t>(g_cfg.height);
        hz = g_cfg.refresh > 0 ? static_cast<uint32_t>(g_cfg.refresh) : BestRefresh(w, h);
        if (!hz) hz = 60;
        Log("forcing %ux%u@%u from dh2013ws.ini", w, h, hz);
    }
    return g_origInit(r, adapter, b, w, h, hz, c, d);
}

// ---- naked stubs jumped to from the engine ----------------------------------------------------
uintptr_t g_retModeAccept, g_retModeReject, g_retCamera, g_retEffect, g_retTrigger, g_retGuiAspect, g_retDrop;
float g_effectOut;

// pushad keeps the integer registers; our C++ may use any xmm register, so keep those too.
#define SAVE_XMM                                                                                         \
    __asm sub esp, 0x80 __asm movdqu xmmword ptr[esp], xmm0 __asm movdqu xmmword ptr[esp + 0x10], xmm1    \
    __asm movdqu xmmword ptr[esp + 0x20], xmm2 __asm movdqu xmmword ptr[esp + 0x30], xmm3                 \
    __asm movdqu xmmword ptr[esp + 0x40], xmm4 __asm movdqu xmmword ptr[esp + 0x50], xmm5                 \
    __asm movdqu xmmword ptr[esp + 0x60], xmm6 __asm movdqu xmmword ptr[esp + 0x70], xmm7
// Restores without touching EFLAGS (lea, not add).
#define RESTORE_XMM                                                                                      \
    __asm movdqu xmm0, xmmword ptr[esp] __asm movdqu xmm1, xmmword ptr[esp + 0x10]                        \
    __asm movdqu xmm2, xmmword ptr[esp + 0x20] __asm movdqu xmm3, xmmword ptr[esp + 0x30]                 \
    __asm movdqu xmm4, xmmword ptr[esp + 0x40] __asm movdqu xmm5, xmmword ptr[esp + 0x50]                 \
    __asm movdqu xmm6, xmmword ptr[esp + 0x60] __asm movdqu xmm7, xmmword ptr[esp + 0x70]                 \
    __asm lea esp, [esp + 0x80]

// Renderer::Init mode loop. esi = Renderer.
__declspec(naked) void ModeStub() {
    __asm pushad
    SAVE_XMM
    __asm push esi
    __asm call AcceptMode
    __asm test al, al
    RESTORE_XMM
    __asm popad
    __asm jnz accept
    __asm jmp dword ptr[g_retModeReject]
accept:
    __asm jmp dword ptr[g_retModeAccept]
}

// Camera::UpdateCachedData. esi = Camera, eax = g_renderer; outputs fov at [ebp-4], aspect at [ebp-0xc].
__declspec(naked) void CameraStub() {
    __asm pushad
    SAVE_XMM
    __asm lea ecx, [ebp - 0xc]
    __asm push ecx
    __asm lea ecx, [ebp - 4]
    __asm push ecx
    __asm push eax
    __asm push esi
    __asm call CameraFov
    RESTORE_XMM
    __asm popad
    __asm jmp dword ptr[g_retCamera]
}

// EffectManager::advanceTimeFov. edx = g_renderer, [ebp-4] = player camera FOV; result goes to xmm0.
__declspec(naked) void EffectStub() {
    __asm pushad
    SAVE_XMM
    __asm push edx
    __asm push dword ptr[ebp - 4]
    __asm call EffectFov
    __asm fstp dword ptr[g_effectOut]
    RESTORE_XMM
    __asm popad
    __asm movss xmm0, dword ptr[g_effectOut]
    __asm jmp dword ptr[g_retEffect]
}

// ViewTrigger::IsInActiveRange. ecx = g_renderer, FOV in and out at [ebp+8].
__declspec(naked) void TriggerStub() {
    __asm pushad
    SAVE_XMM
    __asm push ecx
    __asm push dword ptr[ebp + 8]
    __asm call TriggerFov
    __asm fstp dword ptr[ebp + 8]
    RESTORE_XMM
    __asm popad
    __asm jmp dword ptr[g_retTrigger]
}

// Gui::SetPhysicalViewport, wide screens: [ebp+8] = width, [ebp+0xc] = height (as floats); xmm0 = h/w
// instead of 0.5625, so the 720-high UI canvas gets the screen's real width.
__declspec(naked) void GuiAspectStub() {
    __asm movss xmm0, dword ptr[ebp + 0xc]
    __asm divss xmm0, dword ptr[ebp + 8]
    __asm jmp dword ptr[g_retGuiAspect]
}

// wsModeAimWeapon::ChangeDropMarkers. eax = g_renderer; replaces fld [eax+0x1409c] with 0.5625 on wide screens.
__declspec(naked) void DropStub() {
    __asm fld dword ptr[eax + 0x1409c]
    __asm cmp byte ptr[eax + 0x140ac], 0
    __asm je done
    __asm fstp st(0)
    __asm fld dword ptr[g_dropAspect]
done:
    __asm jmp dword ptr[g_retDrop]
}

// Jumps from `site` to `stub`; the code up to the stub's return address is skipped.
bool Reroute(const char* what, uintptr_t site, const Expect& e, void* stub) {
    bool ok = hook::Jump(At(site), stub, e.n, e.b);
    Log("%s: %s", what, ok ? "patched" : "NOT patched (unexpected code)");
    return ok;
}

// Points the memory operand of the instruction at `site` (`opcode` bytes, then the address of the engine
// constant at `constRva`) at `repl` instead.
bool Redirect(uintptr_t site, std::initializer_list<uint8_t> opcode, uintptr_t constRva, void* repl) {
    Expect e;
    e.Bytes(opcode).Addr(constRva);
    if (std::memcmp(reinterpret_cast<void*>(At(site)), e.b, e.n) != 0) return false;
    return hook::WritePtr(reinterpret_cast<void*>(At(site) + opcode.size()), repl);
}

bool BytesAt(uintptr_t rva, std::initializer_list<uint8_t> bytes) {
    return std::memcmp(reinterpret_cast<void*>(At(rva)), bytes.begin(), bytes.size()) == 0;
}

}  // namespace

bool InstallPatches() {
    // `cmp byte ptr [reg+0x140ac], 0` opens each widening block; the bytes where the stubs return are checked too.
    const uint8_t retCamera[] = {0xD9, 0x86, 0x04};  // fld dword ptr [esi+0x104]
    const uint8_t retEffect[] = {0xF3, 0x0F, 0x11};  // movss [edi+0xa8], xmm0
    const uint8_t retTrigger[] = {0xD9, 0x45, 0xFC}; // fld dword ptr [ebp-4]
    const uint8_t retAccept[] = {0x8B, 0x86, 0x48};  // mov eax, [esi+0x12148]
    if (std::memcmp(reinterpret_cast<void*>(At(kCameraFovEnd)), retCamera, 3) ||
        std::memcmp(reinterpret_cast<void*>(At(kEffectFovEnd)), retEffect, 3) ||
        std::memcmp(reinterpret_cast<void*>(At(kTriggerFovEnd)), retTrigger, 3) ||
        std::memcmp(reinterpret_cast<void*>(At(kModeAccept)), retAccept, 3)) {
        Log("unexpected engine code: mod disabled");
        return false;
    }
    g_retModeAccept = At(kModeAccept);
    g_retModeReject = At(kModeReject);
    g_retCamera = At(kCameraFovEnd);
    g_retEffect = At(kEffectFovEnd);
    g_retTrigger = At(kTriggerFovEnd);

    g_refFov = *reinterpret_cast<const float*>(At(kDefaultFov));
    if (!(g_refFov > 0.1 && g_refFov < 3.0)) g_refFov = 60.0 * kDeg;
    SetUserFov(g_cfg.fov);
    Log("config: AllModes=%d MinRefresh=%d Width=%d Height=%d Refresh=%d FixAspect=%d FOV=%.1f (game default %.1f) "
        "ZoomBlend=%.2f FixUI=%d CrosshairFOV=%d",
        g_cfg.allModes, g_cfg.minRefresh, g_cfg.width, g_cfg.height, g_cfg.refresh, g_cfg.fixAspect, g_cfg.fov,
        g_refFov / kDeg, g_cfg.zoomBlend, g_cfg.fixUI, g_cfg.crosshairFov);

    if (g_cfg.allModes) {
        Expect e;
        e.Bytes({0x83, 0x3C, 0xBD}).Addr(kWidthTable).Bytes({0x00, 0x74, 0x21, 0x83, 0x3C, 0x95});
        e.Addr(kHeightTable).Bytes({0x00, 0x74, 0x17, 0x83, 0x3C, 0x8D}).Addr(kRefreshTable).Bytes({0x00, 0x74, 0x0D});
        Reroute("display mode list", kModeFilter, e, &ModeStub);
    }
    if (g_cfg.width > 0 && g_cfg.height > 0) {
        const uint8_t pro[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x24, 0x01, 0x00, 0x00};  // push ebp; mov ebp,esp; sub esp,124h
        g_origInit = reinterpret_cast<InitFn>(hook::Detour(At(kRendererInit), &InitHook, sizeof pro, pro));
        Log("forced resolution: %s", g_origInit ? "hooked" : "NOT hooked");
    }
    if (g_cfg.fixAspect || g_cfg.fixUI) {
        const uint8_t pro[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08};  // push ebp; mov ebp,esp; sub esp,8
        g_origSetViewport = reinterpret_cast<SetViewportFn>(hook::Detour(At(kSetViewport), &SetViewportHook, sizeof pro, pro));
        Log("real aspect ratio: %s", g_origSetViewport ? "hooked" : "NOT hooked");
    }
    if (g_cfg.fixAspect && BytesAt(kDropAspectEnd, {0xD8, 0x4D, 0x08})) {  // fmul dword ptr [ebp+8]
        g_retDrop = At(kDropAspectEnd);
        Reroute("bullet-drop markers", kDropAspect, Expect().Bytes({0xD9, 0x80, 0x9C, 0x40, 0x01, 0x00}), &DropStub);
    }
    if (g_cfg.fixUI) {
        if (BytesAt(kGuiAspectEnd, {0x8B, 0x91, 0xF8})) {  // mov edx, [ecx+0x2f8]
            g_retGuiAspect = At(kGuiAspectEnd);
            Expect e;
            e.Bytes({0xF3, 0x0F, 0x10, 0x05}).Addr(kConst169);  // movss xmm0, [0.5625]
            Reroute("UI aspect", kGuiAspect, e, &GuiAspectStub);
        }
        bool scope = Redirect(kScopeDiv, {0xDC, 0x35}, kConstScopeDiv, &g_scopeDiv);  // fdiv qword ptr [1280]
        Log("scope overlay size: %s", scope ? "patched" : "NOT patched (unexpected code)");
    }
    if (g_cfg.crosshairFov) {
        bool xhair = Redirect(kCrosshairMul, {0xDC, 0x0D}, kConstCrosshair, &g_crosshairMul);  // fmul qword ptr [42]
        Log("crosshair FOV scaling: %s", xhair ? "patched" : "NOT patched (unexpected code)");
    }
    int quad = 0;
    for (int i = 0; i < 2; i++) {
        quad += Redirect(kQuadNeg[i], {0xF3, 0x0F, 0x10, 0x05}, kQuadNegConst, &g_quadNeg);  // movss xmm0, [-7000]
        quad += Redirect(kQuadPos[i], {0xF3, 0x0F, 0x10, 0x0D}, kQuadPosConst, &g_quadPos);  // movss xmm1, [7000]
    }
    Log("ambient shading quad: %d of 4 patched", quad);
    Reroute("camera FOV", kCameraFov, Expect().Bytes({0x80, 0xB8, 0xAC, 0x40, 0x01, 0x00, 0x00}), &CameraStub);
    Reroute("player FOV", kEffectFov, Expect().Bytes({0x80, 0xBA, 0xAC, 0x40, 0x01, 0x00, 0x00}), &EffectStub);
    Reroute("view trigger FOV", kTriggerFov, Expect().Bytes({0x80, 0xB9, 0xAC, 0x40, 0x01, 0x00, 0x00}), &TriggerStub);

    int grass = 0;
    for (uintptr_t site : kGrass43) {
        Expect e;
        e.Bytes({0xDC, 0x35}).Addr(kConst43);  // fdiv qword ptr [4/3]
        if (std::memcmp(reinterpret_cast<void*>(At(site)), e.b, e.n) == 0 &&
            hook::WritePtr(reinterpret_cast<void*>(At(site) + 2), &g_basis43))
            grass++;
    }
    Log("grass extents: %d of 4 patched", grass);
    return true;
}

#ifdef DH2013WS_TEST
extern "C" __declspec(dllexport) void dh2013ws_test_setfov(float deg) { SetUserFov(deg); }
extern "C" __declspec(dllexport) void dh2013ws_test_zoomblend(float z) { g_cfg.zoomBlend = z; }
#endif
