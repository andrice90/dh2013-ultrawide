// Offline test: maps the game's CloakNTEngine.dll without running it (no imports, no DllMain), loads the test
// build of our dinput8.dll (which patches the mapped engine), then runs each patched engine block on a fake
// Renderer / Camera / stack frame and checks the numbers. No window, no game.
// Usage: harness.exe <path of CloakNTEngine.dll>   (run from the folder holding dinput8.dll + dh2013ws.ini)
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>

static int g_fail;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (cond) {                                       \
            printf("  ok   ");                            \
        } else {                                          \
            printf("  FAIL ");                            \
            g_fail++;                                     \
        }                                                 \
        printf(__VA_ARGS__);                              \
        printf("\n");                                     \
    } while (0)
static bool Near(double a, double b, double eps = 0.01) { return std::fabs(a - b) <= eps; }

static const double kDeg = 3.14159265358979323846 / 180.0;
static uintptr_t g_base;
static uintptr_t At(uintptr_t rva) { return g_base + rva; }

// Same RVAs as src/patches.cpp.
enum : uintptr_t {
    kRendererInit = 0x256780, kModeFilter = 0x256885, kModeAccept = 0x2568a3, kModeReject = 0x2568b0,
    kSetViewport = 0x255140, kCameraFov = 0x31056d, kCameraFovEnd = 0x3105cc, kEffectFov = 0x1eaefe,
    kEffectFovEnd = 0x1eaf48, kTriggerFov = 0x395eee, kTriggerFovEnd = 0x395f33, kConst43 = 0x6af018,
    kRendererPtr = 0x945f30, kGuiAspect = 0x25c60d, kSetPhysicalViewport = 0x25c5c0, kCrosshairMul = 0x2dc44e,
    kScopeDiv = 0x3bf4ff, kDropAspect = 0x3ba871, kDropAspectEnd = 0x3ba877,
};
static const uintptr_t kGrass43[4] = {0x22b1e4, 0x22b8ac, 0x22c969, 0x22e9de};
static const uintptr_t kQuadSites[4] = {0x22fb43, 0x22fb4b, 0x2303d3, 0x2303db};  // movss xmm, [+-7000]
template <typename T>
static T* Operand(uintptr_t rva, size_t at) { return *reinterpret_cast<T**>(g_base + rva + at); }

static void WriteJmp(uintptr_t at, const void* dest) {
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(at), 5, PAGE_EXECUTE_READWRITE, &old);
    auto* p = reinterpret_cast<uint8_t*>(at);
    p[0] = 0xE9;
    *reinterpret_cast<int32_t*>(p + 1) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(dest) - at - 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

// ---- fake engine objects ---------------------------------------------------------------------
static uint8_t* g_ren;  // Renderer (0x20000 bytes)
static uint8_t* g_cam;  // Camera (0x400 bytes)
static float& RF(size_t off) { return *reinterpret_cast<float*>(g_ren + off); }

// ---- run a patched engine block: jump into it with the registers it expects, catch where it ends ----
static uintptr_t g_site, g_saveEsp, g_catchEsp;
static uint32_t g_regEax, g_regEcx, g_regEdx, g_regEsi;
static float g_frame4, g_frameC, g_xmm0Out, g_xmm3Out;
static float kXmm3 = 1234.5f;  // not const: inline asm reads it from memory

// Runs the block at g_site in a fresh frame: [ebp-4] = g_frame4, [ebp-0xc] = g_frameC, [ebp+8] = arg.
// The catchers below are jumped to from the block's end and return to our caller.
__declspec(naked) void __cdecl RunBlock(float /*arg at [ebp+8]*/) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x40
        pushad
        mov eax, g_frame4
        mov [ebp - 4], eax
        mov eax, g_frameC
        mov [ebp - 0xc], eax
        movss xmm3, kXmm3
        mov g_saveEsp, esp
        mov eax, g_regEax
        mov ecx, g_regEcx
        mov edx, g_regEdx
        mov esi, g_regEsi
        jmp dword ptr [g_site]
    }
}
// Shared tail: records what the block left behind and unwinds RunBlock's frame.
__declspec(naked) void CatchBlock() {
    __asm {
        mov g_catchEsp, esp
        mov eax, [ebp - 4]
        mov g_frame4, eax
        mov eax, [ebp - 0xc]
        mov g_frameC, eax
        mov eax, [ebp + 8]
        mov g_regEcx, eax  // reuse as "arg after"
        movss g_xmm0Out, xmm0
        movss g_xmm3Out, xmm3
        mov esp, g_saveEsp
        popad
        mov esp, ebp
        pop ebp
        ret
    }
}
static uint32_t g_catchWhich;
__declspec(naked) void CatchAccept() {
    __asm mov g_catchWhich, 1
    __asm jmp CatchBlock
}
__declspec(naked) void CatchReject() {
    __asm mov g_catchWhich, 2
    __asm jmp CatchBlock
}
// Blocks that leave their result on the x87 stack.
static float g_x87Out;
__declspec(naked) void CatchX87() {
    __asm fstp g_x87Out
    __asm jmp CatchBlock
}

// Renderer::Init is caught right after its prologue (which our trampoline runs): read w/h/hz, return true.
static uint32_t g_initW, g_initH, g_initHz;
__declspec(naked) void CatchInit() {
    __asm {
        mov eax, [ebp + 0x10]
        mov g_initW, eax
        mov eax, [ebp + 0x14]
        mov g_initH, eax
        mov eax, [ebp + 0x18]
        mov g_initHz, eax
        mov esp, ebp
        pop ebp
        mov eax, 1
        ret 0x1c
    }
}

static float RunFovBlock(uintptr_t site, float fov, float* aspectOut = nullptr) {
    g_site = At(site);
    g_frame4 = fov;
    g_frameC = -1.0f;
    g_regEax = g_regEcx = g_regEdx = reinterpret_cast<uint32_t>(g_ren);
    g_regEsi = reinterpret_cast<uint32_t>(g_cam);
    *reinterpret_cast<float*>(g_cam + 0xf8) = fov;
    g_catchEsp = 0;
    RunBlock(fov);
    if (g_catchEsp != g_saveEsp) printf("  FAIL stack moved by %d bytes in block %06X\n", int(g_catchEsp - g_saveEsp), unsigned(site)), g_fail++;
    if (g_xmm3Out != kXmm3) printf("  FAIL xmm3 not preserved in block %06X\n", unsigned(site)), g_fail++;
    if (aspectOut) *aspectOut = g_frameC;
    if (site == kCameraFov) return g_frame4;
    if (site == kEffectFov) return g_xmm0Out;
    return *reinterpret_cast<float*>(&g_regEcx);  // trigger: [ebp+8]
}

static bool RunMode(uint32_t count, uint32_t w, uint32_t h, uint32_t hz) {
    *reinterpret_cast<uint32_t*>(g_ren + 0x12148) = count;
    uint32_t* m = reinterpret_cast<uint32_t*>(g_ren + 0x1214c + count * 16);
    m[0] = w, m[1] = h, m[2] = hz, m[3] = 0x16;
    g_site = At(kModeFilter);
    g_regEsi = reinterpret_cast<uint32_t>(g_ren);
    g_catchWhich = 0;
    RunBlock(0.0f);
    if (g_catchEsp != g_saveEsp) printf("  FAIL stack moved in mode filter\n"), g_fail++;
    return g_catchWhich == 1;
}

using SetViewportFn = void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
using InitFn = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using SetFovFn = void (*)(float);

static double HorPlus(double fovDeg, double hOverW, double k = 1.0) {
    return 2.0 * std::atan(std::tan(fovDeg * kDeg / 2) * (1.0 / hOverW) / (4.0 / 3.0) * k) / kDeg;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: harness <CloakNTEngine.dll>\n");
        return 2;
    }
    HMODULE eng = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!eng) {
        printf("FAIL cannot map %s (%lu)\n", argv[1], GetLastError());
        return 2;
    }
    g_base = reinterpret_cast<uintptr_t>(eng);
    printf("engine mapped at %08X\n", unsigned(g_base));

    char dll[MAX_PATH];
    GetModuleFileNameA(nullptr, dll, MAX_PATH);
    char* name = strrchr(dll, '\\') + 1;
    strcpy_s(name, sizeof dll - (name - dll), "dinput8.dll");
    HMODULE mod = LoadLibraryA(dll);
    if (!mod) {
        printf("FAIL cannot load %s\n", dll);
        return 2;
    }
    auto setFov = reinterpret_cast<SetFovFn>(GetProcAddress(mod, "dh2013ws_test_setfov"));

    printf("patches\n");
    for (uintptr_t site : std::initializer_list<uintptr_t>{kModeFilter, kCameraFov, kEffectFov, kTriggerFov, kSetViewport,
                                                          kRendererInit, kGuiAspect, kDropAspect})
        CHECK(*reinterpret_cast<uint8_t*>(At(site)) == 0xE9, "jump written at %06X", unsigned(site));
    int quad = 0;
    for (int i = 0; i < 4; i++) quad += std::fabs(*Operand<float>(kQuadSites[i], 4)) == 50000.0f;
    CHECK(quad == 4, "ambient shading quad widened to +-50000 (%d of 4)", quad);
    double* xhair = Operand<double>(kCrosshairMul, 2);
    double* scopeDiv = Operand<double>(kScopeDiv, 2);
    CHECK(*xhair == 42.0 && *scopeDiv == 1280.0, "crosshair (%.1f) and scope (%.1f) constants redirected", *xhair,
          *scopeDiv);
    double* basis = nullptr;
    int grass = 0;
    for (uintptr_t site : kGrass43) {
        double* p = *reinterpret_cast<double**>(At(site) + 2);
        if (reinterpret_cast<uintptr_t>(p) != At(kConst43)) grass++, basis = p;
    }
    CHECK(grass == 4 && basis && Near(*basis, 4.0 / 3.0, 1e-9), "grass 4/3 constant redirected (%d of 4, %.6f)", grass,
          basis ? *basis : 0.0);

    // catchers at the ends of the rerouted blocks
    WriteJmp(At(kCameraFovEnd), &CatchBlock);
    WriteJmp(At(kEffectFovEnd), &CatchBlock);
    WriteJmp(At(kTriggerFovEnd), &CatchBlock);
    WriteJmp(At(kModeAccept), &CatchAccept);
    WriteJmp(At(kModeReject), &CatchReject);
    WriteJmp(At(kRendererInit + 9), &CatchInit);
    WriteJmp(At(kDropAspectEnd), &CatchX87);

    g_ren = static_cast<uint8_t*>(_aligned_malloc(0x20000, 16));
    g_cam = static_cast<uint8_t*>(_aligned_malloc(0x400, 16));
    memset(g_ren, 0, 0x20000);
    memset(g_cam, 0, 0x400);
    *reinterpret_cast<uint8_t**>(At(kRendererPtr)) = g_ren;

    printf("Renderer::SetViewport\n");
    auto setViewport = reinterpret_cast<SetViewportFn>(At(kSetViewport));
    struct { uint32_t w, h; float aspect, gui; bool wide; } vp[] = {
        {3440, 1440, 1440.0f / 3440, 1440.0f / 3440, true}, {2560, 1080, 1080.0f / 2560, 1080.0f / 2560, true},
        {1920, 1080, 0.5625f, 0.5625f, true},              {1920, 1200, 0.625f, 0.625f, true},
        {1280, 1024, 0.75f, 0.75f, false},                 {1024, 768, 0.75f, 0.75f, false},
    };
    for (auto& v : vp) {
        setViewport(g_ren, 0, 0, v.w, v.h);
        CHECK(Near(RF(0x1409c), v.aspect, 1e-5) && Near(RF(0x140a0), v.aspect, 1e-5) && Near(RF(0x140b0), v.gui, 1e-5) &&
                  (g_ren[0x140ac] != 0) == v.wide,
              "%ux%u: 3D h/w %.4f (want %.4f), GUI h/w %.4f, wide %d", v.w, v.h, RF(0x1409c), v.aspect, RF(0x140b0),
              g_ren[0x140ac]);
    }

    printf("UI at 3440x1440\n");
    setViewport(g_ren, 0, 0, 3440, 1440);
    CHECK(*scopeDiv == 1720.0, "scope overlay divisor %.1f (720 * 3440 / 1440 = 1720: same size per height as 16:9)",
          *scopeDiv);
    {
        using SetPhysFn = void(__thiscall*)(void*, uint32_t, uint32_t, float);
        auto setPhys = reinterpret_cast<SetPhysFn>(At(kSetPhysicalViewport));
        uint8_t* gui = static_cast<uint8_t*>(_aligned_malloc(0x400, 16));
        memset(gui, 0, 0x400);
        struct { uint32_t w, h; float aspect; } ui[] = {
            {3440, 1440, 1440.0f / 3440}, {1920, 1080, 0.5625f}, {1920, 1200, 0.625f}, {1024, 768, 0.75f}};
        for (auto& u : ui) {
            setPhys(gui, u.w, u.h, 0.0f);
            float got = *reinterpret_cast<float*>(gui + 0x300);
            CHECK(Near(got, u.aspect, 1e-5) && *reinterpret_cast<float*>(gui + 0x308) == 720.0f,
                  "Gui::SetPhysicalViewport %ux%u: canvas h/w %.4f (want %.4f), height %.0f", u.w, u.h, got, u.aspect,
                  *reinterpret_cast<float*>(gui + 0x308));
        }
    }
    g_site = At(kDropAspect);
    g_regEax = reinterpret_cast<uint32_t>(g_ren);
    RunBlock(0.0f);
    CHECK(g_x87Out == 0.5625f && g_catchEsp == g_saveEsp, "bullet-drop markers use 0.5625 on a wide screen (%.4f)",
          g_x87Out);

    printf("FOV blocks at 3440x1440\n");
    const double hw = 1440.0 / 3440;
    float aspect = 0;
    float cam = RunFovBlock(kCameraFov, float(60 * kDeg), &aspect);
    CHECK(Near(cam / kDeg, HorPlus(60, hw)) && Near(aspect, hw, 1e-5), "camera: 60 -> %.2f deg (want %.2f), aspect %.4f",
          cam / kDeg, HorPlus(60, hw), aspect);
    float eff = RunFovBlock(kEffectFov, float(60 * kDeg));
    CHECK(Near(eff, cam, 1e-5), "player real fov: %.2f deg (same as camera)", eff / kDeg);
    float trg = RunFovBlock(kTriggerFov, float(60 * kDeg));
    CHECK(Near(trg, cam, 1e-5), "view trigger: %.2f deg (same as camera)", trg / kDeg);
    float scope = RunFovBlock(kCameraFov, float(15 * kDeg));
    CHECK(Near(scope / kDeg, HorPlus(15, hw)), "camera: scope 15 -> %.2f deg (want %.2f)", scope / kDeg, HorPlus(15, hw));

    printf("FOV setting 75\n");
    if (setFov) setFov(75.0f);
    const double k = std::tan(75 * kDeg / 2) / std::tan(60 * kDeg / 2);
    cam = RunFovBlock(kCameraFov, float(60 * kDeg));
    CHECK(Near(cam / kDeg, HorPlus(75, hw)), "camera: default view -> %.2f deg (want %.2f)", cam / kDeg, HorPlus(75, hw));
    float wider = RunFovBlock(kCameraFov, float(70 * kDeg));
    CHECK(Near(wider / kDeg, HorPlus(70, hw, k)), "camera: sprint 70 -> %.2f deg (want %.2f, full widening)", wider / kDeg,
          HorPlus(70, hw, k));
    scope = RunFovBlock(kCameraFov, float(15 * kDeg));
    CHECK(Near(scope / kDeg, HorPlus(15, hw)), "camera: scope 15 keeps magnification: %.2f deg (want %.2f)", scope / kDeg,
          HorPlus(15, hw));
    double z = std::tan(30 * kDeg) / std::tan(20 * kDeg), w = (z - 1) / (2.0 - 1);
    float mid = RunFovBlock(kCameraFov, float(40 * kDeg));
    CHECK(Near(mid / kDeg, HorPlus(40, hw, std::pow(k, 1 - w))), "camera: 1.6x zoom blends: %.2f deg (want %.2f)",
          mid / kDeg, HorPlus(40, hw, std::pow(k, 1 - w)));
    CHECK(Near(*basis, 4.0 / 3.0 / k, 1e-6), "grass basis %.4f (want %.4f)", *basis, 4.0 / 3.0 / k);
    eff = RunFovBlock(kEffectFov, float(60 * kDeg));
    trg = RunFovBlock(kTriggerFov, float(60 * kDeg));
    CHECK(Near(eff / kDeg, HorPlus(75, hw)) && Near(trg / kDeg, HorPlus(75, hw)), "player %.2f / trigger %.2f deg follow",
          eff / kDeg, trg / kDeg);
    CHECK(*xhair == 42.0, "crosshair stays at the game's size %.1f (CrosshairMinScale=1)", *xhair);
    auto setXhairMin = reinterpret_cast<SetFovFn>(GetProcAddress(mod, "dh2013ws_test_crosshairmin"));
    if (setXhairMin) setXhairMin(0.5f);
    RunFovBlock(kEffectFov, float(60 * kDeg));
    CHECK(Near(*xhair, 42.0 / k, 1e-3), "min 0.5: crosshair scale %.2f (want 42 / %.3f = %.2f)", *xhair, k, 42.0 / k);
    if (setXhairMin) setXhairMin(0.9f);
    RunFovBlock(kEffectFov, float(60 * kDeg));
    CHECK(Near(*xhair, 42.0 * 0.9, 1e-4), "min 0.9: crosshair scale %.2f (want %.2f)", *xhair, 42.0 * 0.9);
    RunFovBlock(kEffectFov, float(15 * kDeg));
    CHECK(*xhair == 42.0, "crosshair scale back to %.1f while scoped", *xhair);
    if (setXhairMin) setXhairMin(1.0f);
    if (setFov) setFov(50.0f);
    RunFovBlock(kEffectFov, float(60 * kDeg));
    const double k50 = std::tan(25 * kDeg) / std::tan(30 * kDeg);
    CHECK(Near(*xhair, 42.0 / k50, 1e-3), "FOV 50: crosshair grows to %.2f (want %.2f)", *xhair, 42.0 / k50);
    if (setFov) setFov(75.0f);
    float huge = RunFovBlock(kCameraFov, float(170 * kDeg));
    CHECK(Near(huge / kDeg, 170.0), "170 widened past 175 is clamped: %.2f deg", huge / kDeg);

    printf("16:9 screen, default FOV: same as the game\n");
    if (setFov) setFov(0.0f);
    setViewport(g_ren, 0, 0, 1920, 1080);
    // the game's own formula: 2 * atan(tan(fov / 2) * 1.7777778 / 1.3333334), aspect 0.5625
    const double game169 = 2.0 * std::atan(std::tan(30 * kDeg) * 1.7777777910232544 / 1.3333333730697632) / kDeg;
    cam = RunFovBlock(kCameraFov, float(60 * kDeg), &aspect);
    CHECK(Near(cam / kDeg, game169, 1e-3) && aspect == 0.5625f, "camera: 60 -> %.3f deg (game %.3f), aspect %.4f",
          cam / kDeg, game169, aspect);

    printf("4:3 screen\n");
    setViewport(g_ren, 0, 0, 1024, 768);
    cam = RunFovBlock(kCameraFov, float(60 * kDeg), &aspect);
    CHECK(Near(cam / kDeg, 60.0, 1e-3) && Near(aspect, 0.75, 1e-6), "camera: 60 -> %.3f deg, aspect %.2f (unchanged)",
          cam / kDeg, aspect);
    CHECK(Near(*basis, 4.0 / 3.0, 1e-9), "grass basis back to 4/3");
    g_site = At(kDropAspect);
    g_regEax = reinterpret_cast<uint32_t>(g_ren);
    RunBlock(0.0f);
    CHECK(g_x87Out == 0.75f, "bullet-drop markers keep 0.75 (%.4f)", g_x87Out);

    printf("display mode filter\n");
    CHECK(RunMode(0, 3440, 1440, 144), "3440x1440@144 listed");
    CHECK(RunMode(1, 3440, 1440, 240), "3440x1440@240 listed");
    CHECK(!RunMode(2, 3440, 1440, 144), "duplicate 3440x1440@144 hidden");
    CHECK(!RunMode(2, 3440, 1440, 30), "3440x1440@30 hidden (MinRefresh)");
    CHECK(!RunMode(2, 640, 480, 60), "640x480 hidden");
    CHECK(!RunMode(2, 5160, 2160, 60), "5160x2160 hidden (sort key limit)");
    CHECK(RunMode(2, 1920, 1080, 60), "1920x1080@60 listed");
    CHECK(RunMode(254, 2560, 1440, 120) && !RunMode(255, 2560, 1440, 100), "list stops at 255 modes (256 slots)");

    printf("Renderer::Init forced resolution\n");
    auto init = reinterpret_cast<InitFn>(At(kRendererInit));
    uint32_t ok = init(g_ren, 0, 0, 666, 666, 66, 0, 0);
    CHECK((ok & 0xff) == 1 && g_initW == 3440 && g_initH == 1440 && g_initHz >= 50 && g_initHz <= 255,
          "666x666@66 -> %ux%u@%u", g_initW, g_initH, g_initHz);

    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
