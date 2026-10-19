// Engine patches for CloakNTEngine.dll: display mode whitelist, real aspect ratio, Hor+ FOV, unstretched UI.
//
// How the engine handles the screen (docs/engine-notes.md has the details):
//  - Renderer::Init keeps only enumerated modes whose width, height and refresh are in fixed tables
//    (widths up to 2560), so 3440x1440 is never offered and never runs.
//  - Renderer::SetViewport computes h/w but stores 0.5625 (16:9) for anything wider than 3:2, else 0.75.
//    Gui::SetPhysicalViewport rounds the same way, so menus and HUD are stretched on wider screens.
//  - The FOV is a horizontal angle on a 4:3 basis. Camera::UpdateCachedData (projection),
//    EffectManager::advanceTimeFov ("real fov") and ViewTrigger::IsInActiveRange (view cone) widen it for
//    "widescreen" with a fixed 16:9 / 4:3 factor. The grass extents divide by 4/3 and the aspect value.
//  - The deferred ambient passes shade through a fixed view-space quad (+-7000 at depth 3500), which only
//    covers 2 * atan(2) = 126.9 degrees: wider views get unshaded (white) edges.
//
// Nothing here uses fixed addresses. Each patch site is found inside an exported engine function (the engine
// exports ~17,000 C++ names) by a byte pattern, and constants are recognised by their values; struct field
// offsets come from small exported getters. A build of the engine where a pattern no longer matches loses
// only that patch (the log says which). Worked out on the Steam demo; the full game uses the same engine.
#include "dh2013ws.h"
#include <cmath>
#include <cstring>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kMaxFov = 170.0 * kDeg;
constexpr uint32_t kModeSlots = 256;  // Renderer's D3DDISPLAYMODE array

// ---- reading the engine -------------------------------------------------------------------------
size_t g_imageSize;

bool InImage(uintptr_t a, size_t n) { return a >= g_engine && a + n <= g_engine + g_imageSize; }
uint32_t U32(uintptr_t a) { return *reinterpret_cast<const uint32_t*>(a); }
int8_t S8(uintptr_t a) { return *reinterpret_cast<const int8_t*>(a); }
// Value of the float / double an absolute memory operand at `a` points to (NaN if it is outside the image).
double F32At(uintptr_t a) { uintptr_t p = U32(a); return InImage(p, 4) ? *reinterpret_cast<const float*>(p) : NAN; }
double F64At(uintptr_t a) { uintptr_t p = U32(a); return InImage(p, 8) ? *reinterpret_cast<const double*>(p) : NAN; }
bool Same(double a, double b) { return std::fabs(a - b) <= 1e-6 * std::fabs(b); }

uintptr_t Export(const char* name) {
    uintptr_t a = reinterpret_cast<uintptr_t>(GetProcAddress(reinterpret_cast<HMODULE>(g_engine), name));
    if (!a) Log("export %s not found", name);
    return a;
}

// Bytes from `f` to the next exported address (an upper bound for the function's length), at most `max`.
// Keeps pattern searches inside one function.
size_t Span(uintptr_t f, size_t max) {
    if (!f) return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(g_engine + reinterpret_cast<IMAGE_DOS_HEADER*>(g_engine)->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(g_engine + dir.VirtualAddress);
    auto* funcs = reinterpret_cast<const uint32_t*>(g_engine + exp->AddressOfFunctions);
    size_t len = max;
    for (DWORD i = 0; i < exp->NumberOfFunctions; i++) {
        uintptr_t a = g_engine + funcs[i];
        if (a > f && a - f < len) len = a - f;
    }
    return len;
}

// Byte pattern with "??" wildcards, e.g. "80 B8 ?? ?? ?? ?? 00 74 ??".
struct Pattern {
    uint8_t b[64];
    bool any[64];
    size_t n = 0;
    static int Nibble(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }
    explicit Pattern(const char* s) {
        while (s[0] && s[1] && n < 64) {
            if (s[0] == ' ') { s++; continue; }
            any[n] = s[0] == '?';
            b[n] = any[n] ? 0 : static_cast<uint8_t>(Nibble(s[0]) << 4 | Nibble(s[1]));
            n++;
            s += 2;
        }
    }
    bool At(uintptr_t a) const {
        if (!InImage(a, n)) return false;
        const auto* p = reinterpret_cast<const uint8_t*>(a);
        for (size_t i = 0; i < n; i++)
            if (!any[i] && p[i] != b[i]) return false;
        return true;
    }
};

// Calls `accept(match)` for every match of `pattern` in the function at `start`, at most `len` bytes in;
// returns the matches accepted.
template <typename F>
int Scan(uintptr_t start, size_t len, const Pattern& p, F accept, uintptr_t* first = nullptr) {
    int count = 0;
    if (!start) return 0;
    len = Span(start, len);
    for (uintptr_t a = start; a < start + len; a++) {
        if (p.At(a) && accept(a)) {
            if (!count && first) *first = a;
            count++;
        }
    }
    return count;
}
// The single match (0 if there is none or more than one).
uintptr_t FindOne(uintptr_t start, size_t len, const Pattern& p, bool (*accept)(uintptr_t) = nullptr) {
    uintptr_t hit = 0;
    int n = Scan(start, len, p, [&](uintptr_t a) { return !accept || accept(a); }, &hit);
    return n == 1 ? hit : 0;
}
bool Contains(uintptr_t start, size_t len, const Pattern& p) {
    return Scan(start, len, p, [](uintptr_t) { return true; }) > 0;
}

// Named addresses found at startup (logged, and read by the offline tests).
struct Note {
    const char* name;
    uintptr_t value;
};
Note g_notes[64];
int g_noteCount;
uintptr_t Noted(const char* name, uintptr_t value) {
    if (g_noteCount < 64) g_notes[g_noteCount++] = {name, value};
    return value;
}

// ---- engine layout, resolved at startup -----------------------------------------------------------
uint32_t g_offAspect, g_offAspectFull, g_offWide, g_offAspectGui;  // Renderer fields
uint32_t g_offModeCount, g_offModes;                                // Renderer display mode list
uint32_t g_offCamFov;                                               // Camera field (radians)

float RF(const uint8_t* r, uint32_t off) { return *reinterpret_cast<const float*>(r + off); }

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
    if (r && r[g_offWide]) {
        float aspect = RF(r, g_offAspect);  // h/w
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
    *fovOut = RealFov(RF(cam, g_offCamFov), r);
    *aspectOut = RF(r, g_offAspect);
}

float __stdcall EffectFov(float f, const uint8_t* r) {
    PollFovKeys();
    // Log what the default (unzoomed) view renders at whenever that changes.
    static float lastNormal;
    float normal = RealFov(static_cast<float>(g_refFov), r);
    if (std::fabs(normal - lastNormal) > 0.001f) {
        lastNormal = normal;
        Log("default view renders %.1f degrees horizontal (aspect h/w %.4f)", normal / kDeg, r ? RF(r, g_offAspect) : 0.0f);
    }
    // The crosshair is sized for the game's FOV; with a wider view the same spread covers less of the screen.
    // Its arms sit at the edges of an 84-unit box (pistol arms are 42 long), so it never goes below
    // CrosshairMinScale of the game's size.
    double scale = 1.0 / FovScale(f);
    if (scale < g_cfg.crosshairMinScale) scale = g_cfg.crosshairMinScale;
    g_crosshairMul = g_cfg.crosshairFov ? 42.0 * scale : 42.0;
    return RealFov(f, r);
}

float __stdcall TriggerFov(float f, const uint8_t* r) { return RealFov(f, r); }

bool __stdcall AcceptMode(const uint8_t* r) {
    uint32_t count = *reinterpret_cast<const uint32_t*>(r + g_offModeCount);
    const uint32_t* m = reinterpret_cast<const uint32_t*>(r + g_offModes + count * 16);
    uint32_t w = m[0], h = m[1], hz = m[2];
    // The next mode is enumerated into slot `count`, so keep one slot free.
    if (count + 2 > kModeSlots) return false;
    // The engine sorts modes by ((w << 12 | h) << 8 | hz): keep the fields inside their bits.
    if (w < 800 || h < 600 || w > 4095 || h > 4095) return false;
    if (hz < static_cast<uint32_t>(g_cfg.minRefresh) || hz > 255) return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t* o = reinterpret_cast<const uint32_t*>(r + g_offModes + i * 16);
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
            *reinterpret_cast<float*>(r + g_offAspect) = real;
            *reinterpret_cast<float*>(r + g_offAspectFull) = real;
        }
        if (g_cfg.fixUI) {
            *reinterpret_cast<float*>(r + g_offAspectGui) = real;
            g_scopeDiv = 720.0 * w / h;
        }
    }
    static uint32_t lastW, lastH;
    if (w != lastW || h != lastH) {
        lastW = w;
        lastH = h;
        Log("viewport %ux%u at %u,%u: 3D aspect h/w %.4f, UI h/w %.4f", w, h, x, y, RF(r, g_offAspect),
            RF(r, g_offAspectGui));
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

// wsModeAimWeapon::ChangeDropMarkers. eax = g_renderer; replaces fld [eax+aspect] with 0.5625 on wide screens.
__declspec(naked) void DropStub() {
    __asm push ecx
    __asm mov ecx, dword ptr[g_offAspect]
    __asm fld dword ptr[eax + ecx]
    __asm mov ecx, dword ptr[g_offWide]
    __asm cmp byte ptr[eax + ecx], 0
    __asm pop ecx
    __asm je done
    __asm fstp st(0)
    __asm fld dword ptr[g_dropAspect]
done:
    __asm jmp dword ptr[g_retDrop]
}

// ---- finding the patch sites ------------------------------------------------------------------
// Struct offset read by a getter `fld/mov reg, [ecx+disp32]; ret`.
uint32_t GetterOffset(const char* name, const char* pattern) {
    uintptr_t f = Export(name);
    return f && Pattern(pattern).At(f) ? U32(f + 2) : 0;
}

bool ResolveLayout() {
    g_offAspect = GetterOffset("?GetGameAspectRatio@Renderer@@QAEMXZ", "D9 81 ?? ?? ?? ?? C3");
    g_offWide = GetterOffset("?GetGameWideScreen@Renderer@@QAE_NXZ", "8A 81 ?? ?? ?? ?? C3");
    g_offAspectGui = GetterOffset("?GetAspectRatio@Renderer@@QAEMXZ", "D9 81 ?? ?? ?? ?? C3");
    g_offCamFov = GetterOffset("?GetFov@Camera@@QAEMXZ", "D9 81 ?? ?? ?? ?? C3");
    // BeginRenderingFullScreen copies the full-screen aspect into the 3D one: fld [ecx+full]; fstp [ecx+aspect]
    uintptr_t full = 0;
    Scan(Export("?BeginRenderingFullScreen@Renderer@@QAEXXZ"), 0x40, Pattern("D9 81 ?? ?? ?? ?? D9 99 ?? ?? ?? ??"),
         [](uintptr_t a) { return U32(a + 8) == g_offAspect; }, &full);
    g_offAspectFull = full ? U32(full + 2) : 0;
    Log("renderer layout: aspect +%X, full-screen aspect +%X, widescreen flag +%X, UI aspect +%X; camera FOV +%X",
        g_offAspect, g_offAspectFull, g_offWide, g_offAspectGui, g_offCamFov);
    Noted("off.aspect", g_offAspect), Noted("off.aspectFull", g_offAspectFull), Noted("off.wide", g_offWide);
    Noted("off.aspectGui", g_offAspectGui), Noted("off.camFov", g_offCamFov);
    return g_offAspect && g_offAspectFull && g_offWide && g_offAspectGui && g_offCamFov;
}

// A widening block "cmp byte ptr [reg+wide], 0; je end" (plus `tail` bytes) inside `func`. Returns the
// block start and sets `end` to the je target.
uintptr_t WideBlock(const char* func, size_t window, const char* pattern, size_t jePos, uintptr_t* end) {
    uintptr_t f = Export(func);
    uintptr_t site = FindOne(f, window, Pattern(pattern), [](uintptr_t a) { return U32(a + 2) == g_offWide; });
    if (site) *end = site + jePos + 2 + S8(site + jePos + 1);
    return site;
}

bool Report(const char* what, bool ok) {
    Log("%s: %s", what, ok ? "patched" : "NOT patched (code not recognised)");
    return ok;
}

bool Jump(uintptr_t site, size_t len, void* stub) {
    return site && hook::Jump(site, stub, len, reinterpret_cast<const uint8_t*>(site));
}

}  // namespace

bool InstallPatches() {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_engine);
    g_imageSize = reinterpret_cast<IMAGE_NT_HEADERS32*>(g_engine + dos->e_lfanew)->OptionalHeader.SizeOfImage;
    if (!ResolveLayout()) {
        Log("engine layout not recognised: mod disabled");
        return false;
    }

    // Default player FOV: EffectManager::advanceTimeFov starts with movss xmm0, [default].
    uintptr_t fovFunc = Export("?advanceTimeFov@EffectManager@@QAEXMPAVActor@@PAVcsControllerActorPlayer@@@Z");
    uintptr_t fovLoad = FindOne(fovFunc, 0x20, Pattern("F3 0F 10 05 ?? ?? ?? ??"));
    double ref = fovLoad ? F32At(fovLoad + 4) : NAN;
    g_refFov = ref > 0.1 && ref < 3.0 ? ref : 60.0 * kDeg;
    SetUserFov(g_cfg.fov);
    Log("config: AllModes=%d MinRefresh=%d Width=%d Height=%d Refresh=%d FixAspect=%d FOV=%.1f (game default %.1f) "
        "ZoomBlend=%.2f FixUI=%d CrosshairFOV=%d CrosshairMinScale=%.2f",
        g_cfg.allModes, g_cfg.minRefresh, g_cfg.width, g_cfg.height, g_cfg.refresh, g_cfg.fixAspect, g_cfg.fov,
        g_refFov / kDeg, g_cfg.zoomBlend, g_cfg.fixUI, g_cfg.crosshairFov, g_cfg.crosshairMinScale);

    // Display modes: Renderer::Init tests each enumerated mode against three tables:
    //   cmp [edi*4+widths],0; je reject; cmp [edx*4+heights],0; je reject; cmp [ecx*4+rates],0; je reject
    // followed by mov eax, [esi+count].
    uintptr_t init = Export("?Init@Renderer@@QAE_NI_NIII00@Z");
    if (g_cfg.allModes) {
        uintptr_t site = FindOne(init, 0x600,
                                 Pattern("83 3C BD ?? ?? ?? ?? 00 74 ?? 83 3C 95 ?? ?? ?? ?? 00 74 ?? "
                                         "83 3C 8D ?? ?? ?? ?? 00 74 ?? 8B 86 ?? ?? ?? ??"));
        if (site) {
            g_retModeAccept = Noted("mode.accept", site + 30);
            g_retModeReject = Noted("mode.reject", site + 30 + S8(site + 29));
            g_offModeCount = U32(site + 32);
            g_offModes = g_offModeCount + 4;
            Noted("off.modeCount", g_offModeCount);
        }
        Report("display mode list", Jump(Noted("mode", site), 5, &ModeStub));
    }
    if (g_cfg.width > 0 && g_cfg.height > 0) {
        bool ok = init && Pattern("55 8B EC 81 EC ?? ?? ?? ??").At(init);  // push ebp; mov ebp,esp; sub esp,imm32
        if (ok) g_origInit = reinterpret_cast<InitFn>(hook::Detour(init, &InitHook, 9, reinterpret_cast<uint8_t*>(init)));
        Report("forced resolution", g_origInit != nullptr);
    }
    if (g_cfg.fixAspect || g_cfg.fixUI) {
        uintptr_t f = Export("?SetViewport@Renderer@@QAEXIIII@Z");
        bool ok = f && Pattern("55 8B EC 83 EC ??").At(f);  // push ebp; mov ebp,esp; sub esp,imm8
        if (ok)
            g_origSetViewport = reinterpret_cast<SetViewportFn>(
                hook::Detour(f, &SetViewportHook, 6, reinterpret_cast<uint8_t*>(f)));
        Report("real aspect ratio", g_origSetViewport != nullptr);
    }

    // Camera::UpdateCachedData: cmp [eax+wide],0; movss xmm0,[esi+fov]; movss [ebp-4],xmm0;
    //   movss xmm0,[eax+aspect]; movss [ebp-0xc],xmm0; je end
    {
        uintptr_t f = Export("?UpdateCachedData@Camera@@QAEXXZ");
        uintptr_t site = FindOne(f, 0x600,
                                 Pattern("80 B8 ?? ?? ?? ?? 00 F3 0F 10 86 ?? ?? ?? ?? F3 0F 11 45 FC "
                                         "F3 0F 10 80 ?? ?? ?? ?? F3 0F 11 45 F4 74 ??"),
                                 [](uintptr_t a) {
                                     return U32(a + 2) == g_offWide && U32(a + 11) == g_offCamFov &&
                                            U32(a + 24) == g_offAspect;
                                 });
        if (site) g_retCamera = Noted("camera.end", site + 35 + S8(site + 34));
        Report("camera FOV", Jump(Noted("camera", site), 7, &CameraStub));
    }
    // EffectManager::advanceTimeFov: cmp [edx+wide],0; je end; fld [ebp-4] ... end: movss [edi+x], xmm0
    {
        uintptr_t end = 0;
        uintptr_t site = WideBlock("?advanceTimeFov@EffectManager@@QAEXMPAVActor@@PAVcsControllerActorPlayer@@@Z",
                                   0x800, "80 BA ?? ?? ?? ?? 00 74 ?? D9 45 FC", 7, &end);
        if (site && !Pattern("F3 0F 11").At(end)) site = 0;
        g_retEffect = Noted("effect.end", end);
        Report("player FOV", Jump(Noted("effect", site), 7, &EffectStub));
    }
    // ViewTrigger::IsInActiveRange: cmp [ecx+wide],0; je end; fld [ebp+8] ...
    {
        uintptr_t end = 0;
        uintptr_t site = WideBlock("?IsInActiveRange@ViewTrigger@@UAE_NPAVControllableObject@@@Z", 0x400,
                                   "80 B9 ?? ?? ?? ?? 00 74 ?? D9 45 08", 7, &end);
        g_retTrigger = Noted("trigger.end", end);
        Report("view trigger FOV", Jump(Noted("trigger", site), 7, &TriggerStub));
    }
    // wsModeAimWeapon::ChangeDropMarkers: fld [eax+aspect]; fmul [ebp+8]
    if (g_cfg.fixAspect) {
        uintptr_t site = FindOne(Export("?ChangeDropMarkers@wsModeAimWeapon@@IAEXM@Z"), 0x400,
                                 Pattern("D9 80 ?? ?? ?? ?? D8 4D 08"),
                                 [](uintptr_t a) { return U32(a + 2) == g_offAspect; });
        if (site) g_retDrop = Noted("drop.end", site + 6);
        Report("bullet-drop markers", Jump(Noted("drop", site), 6, &DropStub));
    }
    if (g_cfg.fixUI) {
        // Gui::SetPhysicalViewport: fld [0.666]; fcomip st(2); fstp st(1); jbe x; movss xmm0,[0.5625]; jmp end
        // (width and height were stored to [ebp+8] / [ebp+0xc] as floats before).
        uintptr_t f = Export("?SetPhysicalViewport@Gui@@QAEXIIM@Z");
        uintptr_t m = FindOne(f, 0x200, Pattern("DD 05 ?? ?? ?? ?? DF F2 DD D9 76 ?? F3 0F 10 05 ?? ?? ?? ?? EB ??"),
                              [](uintptr_t a) { return Same(F64At(a + 2), 0.666f) && Same(F32At(a + 16), 0.5625); });
        bool stores = m && Contains(f, m - f, Pattern("D9 5D 08")) && Contains(f, m - f, Pattern("D9 5D 0C"));
        uintptr_t site = stores ? m + 12 : 0;
        if (site) g_retGuiAspect = Noted("gui.end", m + 22 + S8(m + 21));
        Report("UI aspect", Jump(Noted("gui", site), 8, &GuiAspectStub));

        // wsModeHudBase::RenderHUD sizes the scope overlay as width * 1024 / 1280: fdiv qword ptr [1280]
        uintptr_t scope = FindOne(Export("?RenderHUD@wsModeHudBase@@UAEXXZ"), 0x200, Pattern("DC 35 ?? ?? ?? ??"),
                                  [](uintptr_t a) { return Same(F64At(a + 2), 1280.0); });
        Report("scope overlay size", scope && hook::WritePtr(reinterpret_cast<void*>(Noted("scope", scope + 2)), &g_scopeDiv));
    }
    if (g_cfg.crosshairFov) {
        // PlayerHud::Crosshair_AdvanceTime: fadd st,st; fstp [ebp-x]; fld [ebp-x]; fmul qword ptr [42]
        uintptr_t m = FindOne(Export("?Crosshair_AdvanceTime@PlayerHud@@IAEXM@Z"), 0xA00,
                              Pattern("DC C0 D9 5D ?? D9 45 ?? DC 0D ?? ?? ?? ??"),
                              [](uintptr_t a) { return Same(F64At(a + 10), 42.0); });
        Report("crosshair FOV scaling",
               m && hook::WritePtr(reinterpret_cast<void*>(Noted("crosshair", m + 10)), &g_crosshairMul));
    }

    // Deferred ambient passes: movss xmm0, [-7000] / movss xmm1, [7000] near the start.
    int quad = 0;
    const char* shadeFuncs[] = {"?RenderShadeAmbient@Renderer@@IAEXI@Z", "?RenderShadeAmbientDirectional@Renderer@@IAEXI@Z"};
    static const char* quadNames[] = {"quad0", "quad1", "quad2", "quad3"};
    for (int i = 0; i < 2; i++) {
        uintptr_t f = Export(shadeFuncs[i]);
        uintptr_t neg = FindOne(f, 0x80, Pattern("F3 0F 10 05 ?? ?? ?? ??"), [](uintptr_t a) { return Same(F32At(a + 4), -7000.0); });
        uintptr_t pos = FindOne(f, 0x80, Pattern("F3 0F 10 0D ?? ?? ?? ??"), [](uintptr_t a) { return Same(F32At(a + 4), 7000.0); });
        quad += neg && hook::WritePtr(reinterpret_cast<void*>(Noted(quadNames[2 * i], neg + 4)), &g_quadNeg);
        quad += pos && hook::WritePtr(reinterpret_cast<void*>(Noted(quadNames[2 * i + 1], pos + 4)), &g_quadPos);
    }
    Log("ambient shading quad: %d of 4 patched", quad);

    // Grass extents: fdiv qword ptr [4/3] shortly before fdiv dword ptr [esi+aspect].
    int grass = 0;
    const char* grassFuncs[] = {
        "?RenderSolid@Renderer@@IAEXPAV?$SimpleListDynamic@PAUGEOMETRY_RECORD@@$03@@I@Z",
        "?RenderTextured@Renderer@@IAEXPAV?$SimpleListDynamic@PAUGEOMETRY_RECORD@@$03@@I@Z",
        "?RenderNormalWii@Renderer@@IAEXPAV?$SimpleListDynamic@PAUGEOMETRY_RECORD@@$03@@I@Z",
        "?RenderDefer@Renderer@@IAEXPAV?$SimpleListDynamic@PAUGEOMETRY_RECORD@@$03@@I@Z",
    };
    static const char* grassNames[] = {"grass0", "grass1", "grass2", "grass3"};
    for (const char* fn : grassFuncs) {
        uintptr_t site = FindOne(Export(fn), 0x1000, Pattern("DC 35 ?? ?? ?? ??"), [](uintptr_t a) {
            if (!Same(F64At(a + 2), 4.0f / 3.0f)) return false;
            Pattern aspectDiv("D8 B6 ?? ?? ?? ??");
            for (uintptr_t b = a + 6; b < a + 0x30; b++)
                if (aspectDiv.At(b) && U32(b + 2) == g_offAspect) return true;
            return false;
        });
        if (site && hook::WritePtr(reinterpret_cast<void*>(Noted(grassNames[grass], site + 2)), &g_basis43)) grass++;
    }
    Log("grass extents: %d of 4 patched", grass);
    return true;
}

#ifdef DH2013WS_TEST
extern "C" __declspec(dllexport) void dh2013ws_test_setfov(float deg) { SetUserFov(deg); }
extern "C" __declspec(dllexport) void dh2013ws_test_zoomblend(float z) { g_cfg.zoomBlend = z; }
extern "C" __declspec(dllexport) void dh2013ws_test_crosshairmin(float s) { g_cfg.crosshairMinScale = s; }
// Address (or offset) the mod found for `name` at startup, 0 if none.
extern "C" __declspec(dllexport) uintptr_t dh2013ws_test_note(const char* name) {
    for (int i = 0; i < g_noteCount; i++)
        if (std::strcmp(g_notes[i].name, name) == 0) return g_notes[i].value;
    return 0;
}
#endif
