// Offline test: maps the game's CloakNTEngine.dll without running it (no imports, no DllMain), loads the test
// build of our dinput8.dll (which finds its patch sites and patches the mapped engine), then runs each patched
// engine block on a fake Renderer / Camera / stack frame and checks the numbers. No window, no game.
// Works on any engine build: the sites come from the mod (dh2013ws_test_note). On the Steam demo build they are
// also compared with the addresses verified by hand.
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
static HMODULE g_eng;
static uintptr_t g_base;

// ---- what the mod found ------------------------------------------------------------------------
using NoteFn = uintptr_t (*)(const char*);
static NoteFn g_note;
static uintptr_t Note(const char* name) { return g_note ? g_note(name) : 0; }
static uint32_t g_offAspect, g_offAspectFull, g_offWide, g_offAspectGui, g_offCamFov, g_offModeCount;
template <typename T>
static T* Operand(const char* name) {
    uintptr_t a = Note(name);
    return a ? *reinterpret_cast<T**>(a) : nullptr;
}

// Sites the mod must find on the Steam demo build (RVAs, verified by hand).
static const DWORD kDemoStamp = 1353949478;
static const struct { const char* name; uintptr_t rva; } kDemoSites[] = {
    {"mode", 0x256885},       {"mode.accept", 0x2568a3}, {"mode.reject", 0x2568b0}, {"camera", 0x31056d},
    {"camera.end", 0x3105cc}, {"effect", 0x1eaefe},      {"effect.end", 0x1eaf48},  {"trigger", 0x395eee},
    {"trigger.end", 0x395f33}, {"drop", 0x3ba871},       {"drop.end", 0x3ba877},    {"gui", 0x25c60d},
    {"gui.end", 0x25c61f},    {"crosshair", 0x2dc450},   {"scope", 0x3bf501},       {"quad0", 0x22fb47},
    {"quad1", 0x22fb4f},      {"quad2", 0x2303d7},       {"quad3", 0x2303df},       {"grass0", 0x22b1e6},
    {"grass1", 0x22b8ae},     {"grass2", 0x22c96b},      {"grass3", 0x22e9e0},
};
static const struct { const char* name; uint32_t value; } kDemoOffsets[] = {
    {"off.aspect", 0x1409c}, {"off.aspectFull", 0x140a0}, {"off.wide", 0x140ac},
    {"off.aspectGui", 0x140b0}, {"off.camFov", 0xf8},     {"off.modeCount", 0x12148},
};

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

enum Block { kCamera, kEffect, kTrigger };
static float RunFovBlock(Block b, float fov, float* aspectOut = nullptr) {
    static const char* names[] = {"camera", "effect", "trigger"};
    g_site = Note(names[b]);
    if (!g_site) return -1.0f;
    g_frame4 = fov;
    g_frameC = -1.0f;
    g_regEax = g_regEcx = g_regEdx = reinterpret_cast<uint32_t>(g_ren);
    g_regEsi = reinterpret_cast<uint32_t>(g_cam);
    *reinterpret_cast<float*>(g_cam + g_offCamFov) = fov;
    g_catchEsp = 0;
    RunBlock(fov);
    if (g_catchEsp != g_saveEsp) printf("  FAIL stack moved by %d bytes in %s block\n", int(g_catchEsp - g_saveEsp), names[b]), g_fail++;
    if (g_xmm3Out != kXmm3) printf("  FAIL xmm3 not preserved in %s block\n", names[b]), g_fail++;
    if (aspectOut) *aspectOut = g_frameC;
    if (b == kCamera) return g_frame4;
    if (b == kEffect) return g_xmm0Out;
    return *reinterpret_cast<float*>(&g_regEcx);  // trigger: [ebp+8]
}

static bool RunMode(uint32_t count, uint32_t w, uint32_t h, uint32_t hz) {
    *reinterpret_cast<uint32_t*>(g_ren + g_offModeCount) = count;
    uint32_t* m = reinterpret_cast<uint32_t*>(g_ren + g_offModeCount + 4 + count * 16);
    m[0] = w, m[1] = h, m[2] = hz, m[3] = 0x16;
    g_site = Note("mode");
    g_regEsi = reinterpret_cast<uint32_t>(g_ren);
    g_catchWhich = 0;
    RunBlock(0.0f);
    if (g_catchEsp != g_saveEsp) printf("  FAIL stack moved in mode filter\n"), g_fail++;
    return g_catchWhich == 1;
}

static float RunDrop() {
    g_site = Note("drop");
    g_regEax = reinterpret_cast<uint32_t>(g_ren);
    g_x87Out = -1.0f;
    RunBlock(0.0f);
    if (g_catchEsp != g_saveEsp) printf("  FAIL stack moved in drop marker block\n"), g_fail++;
    return g_x87Out;
}

using SetViewportFn = void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
using SetPhysFn = void(__thiscall*)(void*, uint32_t, uint32_t, float);
using InitFn = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using SetFovFn = void (*)(float);

static double HorPlus(double fovDeg, double hOverW, double k = 1.0) {
    return 2.0 * std::atan(std::tan(fovDeg * kDeg / 2) * (1.0 / hOverW) / (4.0 / 3.0) * k) / kDeg;
}

static uintptr_t Export(const char* name) { return reinterpret_cast<uintptr_t>(GetProcAddress(g_eng, name)); }

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: harness <CloakNTEngine.dll> [--rebase]\n");
        return 2;
    }
    // --rebase: occupy the engine's preferred base so Windows relocates it (no fixed addresses may be assumed).
    if (argc > 2 && strcmp(argv[2], "--rebase") == 0 && !VirtualAlloc(reinterpret_cast<void*>(0x10000000), 0x01000000, MEM_RESERVE, PAGE_NOACCESS))
        printf("note: could not reserve 0x10000000\n");
    g_eng = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!g_eng) {
        printf("FAIL cannot map %s (%lu)\n", argv[1], GetLastError());
        return 2;
    }
    g_base = reinterpret_cast<uintptr_t>(g_eng);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(g_base + reinterpret_cast<IMAGE_DOS_HEADER*>(g_base)->e_lfanew);
    DWORD stamp = nt->FileHeader.TimeDateStamp;
    printf("engine %s\n  mapped at %08X, build %lu%s\n", argv[1], unsigned(g_base), stamp,
           stamp == kDemoStamp ? " (Steam demo)" : "");

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
    auto setXhairMin = reinterpret_cast<SetFovFn>(GetProcAddress(mod, "dh2013ws_test_crosshairmin"));
    g_note = reinterpret_cast<NoteFn>(GetProcAddress(mod, "dh2013ws_test_note"));
    if (!setFov || !setXhairMin || !g_note) {
        printf("FAIL dinput8.dll is not the test build\n");
        return 2;
    }
    g_offAspect = Note("off.aspect"), g_offAspectFull = Note("off.aspectFull"), g_offWide = Note("off.wide");
    g_offAspectGui = Note("off.aspectGui"), g_offCamFov = Note("off.camFov"), g_offModeCount = Note("off.modeCount");

    printf("patch sites\n");
    CHECK(g_offAspect && g_offAspectFull && g_offWide && g_offAspectGui && g_offCamFov && g_offModeCount,
          "layout: aspect +%X, full +%X, wide +%X, UI +%X, camera FOV +%X, mode count +%X", g_offAspect,
          g_offAspectFull, g_offWide, g_offAspectGui, g_offCamFov, g_offModeCount);
    for (const char* site : {"mode", "camera", "effect", "trigger", "drop", "gui"})
        CHECK(Note(site) && *reinterpret_cast<uint8_t*>(Note(site)) == 0xE9, "%s: jump written", site);
    for (const char* fn : {"?SetViewport@Renderer@@QAEXIIII@Z", "?Init@Renderer@@QAE_NI_NIII00@Z"})
        CHECK(*reinterpret_cast<uint8_t*>(Export(fn)) == 0xE9, "%s detoured", fn);
    int quad = 0;
    for (const char* q : {"quad0", "quad1", "quad2", "quad3"}) quad += Operand<float>(q) && std::fabs(*Operand<float>(q)) == 50000.0f;
    CHECK(quad == 4, "ambient shading quad widened to +-50000 (%d of 4)", quad);
    double* xhair = Operand<double>("crosshair");
    double* scopeDiv = Operand<double>("scope");
    CHECK(xhair && scopeDiv && *xhair == 42.0 && *scopeDiv == 1280.0, "crosshair (42) and scope (1280) constants redirected");
    double* basis = Operand<double>("grass0");
    int grass = 0;
    for (const char* g : {"grass0", "grass1", "grass2", "grass3"}) grass += Operand<double>(g) == basis && basis;
    CHECK(grass == 4 && Near(*basis, 4.0 / 3.0, 1e-9), "grass 4/3 constant redirected (%d of 4)", grass);
    if (!xhair || !scopeDiv || !basis || g_fail) {
        printf("%d FAILED (sites missing; no point running the blocks)\n", g_fail);
        return 1;
    }
    if (stamp == kDemoStamp) {
        int same = 0, total = 0;
        for (auto& s : kDemoSites) {
            total++;
            if (Note(s.name) == g_base + s.rva) same++;
            else printf("  FAIL %s found at %08X, verified at %08X\n", s.name, unsigned(Note(s.name)), unsigned(g_base + s.rva)), g_fail++;
        }
        for (auto& o : kDemoOffsets) {
            total++;
            if (Note(o.name) == o.value) same++;
            else printf("  FAIL %s = %X, verified %X\n", o.name, unsigned(Note(o.name)), o.value), g_fail++;
        }
        CHECK(same == total, "demo build: all %d found sites and offsets equal the hand-verified ones", total);
    }

    // catchers at the ends of the rerouted blocks
    for (const char* e : {"camera.end", "effect.end", "trigger.end"}) WriteJmp(Note(e), &CatchBlock);
    WriteJmp(Note("mode.accept"), &CatchAccept);
    WriteJmp(Note("mode.reject"), &CatchReject);
    WriteJmp(Note("drop.end"), &CatchX87);
    WriteJmp(Export("?Init@Renderer@@QAE_NI_NIII00@Z") + 9, &CatchInit);

    g_ren = static_cast<uint8_t*>(_aligned_malloc(0x20000, 16));
    g_cam = static_cast<uint8_t*>(_aligned_malloc(0x400, 16));
    memset(g_ren, 0, 0x20000);
    memset(g_cam, 0, 0x400);

    printf("Renderer::SetViewport\n");
    auto setViewport = reinterpret_cast<SetViewportFn>(Export("?SetViewport@Renderer@@QAEXIIII@Z"));
    struct { uint32_t w, h; float aspect; bool wide; } vp[] = {
        {3440, 1440, 1440.0f / 3440, true}, {2560, 1080, 1080.0f / 2560, true}, {1920, 1080, 0.5625f, true},
        {1920, 1200, 0.625f, true},        {1280, 1024, 0.75f, false},         {1024, 768, 0.75f, false},
    };
    for (auto& v : vp) {
        setViewport(g_ren, 0, 0, v.w, v.h);
        CHECK(Near(RF(g_offAspect), v.aspect, 1e-5) && Near(RF(g_offAspectFull), v.aspect, 1e-5) &&
                  Near(RF(g_offAspectGui), v.aspect, 1e-5) && (g_ren[g_offWide] != 0) == v.wide,
              "%ux%u: 3D h/w %.4f, UI h/w %.4f (want %.4f), wide %d", v.w, v.h, RF(g_offAspect), RF(g_offAspectGui),
              v.aspect, g_ren[g_offWide]);
    }

    printf("UI at 3440x1440\n");
    setViewport(g_ren, 0, 0, 3440, 1440);
    CHECK(*scopeDiv == 1720.0, "scope overlay divisor %.1f (720 * 3440 / 1440: same size per height as 16:9)", *scopeDiv);
    {
        auto setPhys = reinterpret_cast<SetPhysFn>(Export("?SetPhysicalViewport@Gui@@QAEXIIM@Z"));
        uint8_t* gui = static_cast<uint8_t*>(_aligned_malloc(0x400, 16));
        memset(gui, 0, 0x400);
        setPhys(gui, 3440, 1440, 0.0f);
        // the canvas aspect field: the only float equal to 1440 / 3440 in the object
        int field = -1;
        for (int off = 0; off < 0x400; off += 4)
            if (Near(*reinterpret_cast<float*>(gui + off), 1440.0f / 3440, 1e-6)) field = off;
        CHECK(field >= 0, "Gui::SetPhysicalViewport 3440x1440: canvas h/w 0.4186 stored (Gui+%X)", field);
        struct { uint32_t w, h; float aspect; } ui[] = {{1920, 1080, 0.5625f}, {1920, 1200, 0.625f}, {1024, 768, 0.75f}};
        for (auto& u : ui) {
            setPhys(gui, u.w, u.h, 0.0f);
            float got = field >= 0 ? *reinterpret_cast<float*>(gui + field) : -1.0f;
            CHECK(Near(got, u.aspect, 1e-5), "Gui::SetPhysicalViewport %ux%u: canvas h/w %.4f (want %.4f)", u.w, u.h, got, u.aspect);
        }
    }
    CHECK(RunDrop() == 0.5625f, "bullet-drop markers use 0.5625 on a wide screen (%.4f)", g_x87Out);

    printf("FOV blocks at 3440x1440\n");
    const double hw = 1440.0 / 3440;
    float aspect = 0;
    float cam = RunFovBlock(kCamera, float(60 * kDeg), &aspect);
    CHECK(Near(cam / kDeg, HorPlus(60, hw)) && Near(aspect, hw, 1e-5), "camera: 60 -> %.2f deg (want %.2f), aspect %.4f",
          cam / kDeg, HorPlus(60, hw), aspect);
    float eff = RunFovBlock(kEffect, float(60 * kDeg));
    CHECK(Near(eff, cam, 1e-5), "player real fov: %.2f deg (same as camera)", eff / kDeg);
    float trg = RunFovBlock(kTrigger, float(60 * kDeg));
    CHECK(Near(trg, cam, 1e-5), "view trigger: %.2f deg (same as camera)", trg / kDeg);
    float scope = RunFovBlock(kCamera, float(15 * kDeg));
    CHECK(Near(scope / kDeg, HorPlus(15, hw)), "camera: scope 15 -> %.2f deg (want %.2f)", scope / kDeg, HorPlus(15, hw));

    printf("FOV setting 75\n");
    setFov(75.0f);
    const double k = std::tan(75 * kDeg / 2) / std::tan(60 * kDeg / 2);
    cam = RunFovBlock(kCamera, float(60 * kDeg));
    CHECK(Near(cam / kDeg, HorPlus(75, hw)), "camera: default view -> %.2f deg (want %.2f)", cam / kDeg, HorPlus(75, hw));
    float wider = RunFovBlock(kCamera, float(70 * kDeg));
    CHECK(Near(wider / kDeg, HorPlus(70, hw, k)), "camera: sprint 70 -> %.2f deg (want %.2f, full widening)", wider / kDeg,
          HorPlus(70, hw, k));
    scope = RunFovBlock(kCamera, float(15 * kDeg));
    CHECK(Near(scope / kDeg, HorPlus(15, hw)), "camera: scope 15 keeps magnification: %.2f deg (want %.2f)", scope / kDeg,
          HorPlus(15, hw));
    double z = std::tan(30 * kDeg) / std::tan(20 * kDeg), w = (z - 1) / (2.0 - 1);
    float mid = RunFovBlock(kCamera, float(40 * kDeg));
    CHECK(Near(mid / kDeg, HorPlus(40, hw, std::pow(k, 1 - w))), "camera: 1.6x zoom blends: %.2f deg (want %.2f)",
          mid / kDeg, HorPlus(40, hw, std::pow(k, 1 - w)));
    CHECK(Near(*basis, 4.0 / 3.0 / k, 1e-6), "grass basis %.4f (want %.4f)", *basis, 4.0 / 3.0 / k);
    eff = RunFovBlock(kEffect, float(60 * kDeg));
    trg = RunFovBlock(kTrigger, float(60 * kDeg));
    CHECK(Near(eff / kDeg, HorPlus(75, hw)) && Near(trg / kDeg, HorPlus(75, hw)), "player %.2f / trigger %.2f deg follow",
          eff / kDeg, trg / kDeg);
    CHECK(*xhair == 42.0, "crosshair stays at the game's size %.1f (CrosshairMinScale=1)", *xhair);
    setXhairMin(0.5f);
    RunFovBlock(kEffect, float(60 * kDeg));
    CHECK(Near(*xhair, 42.0 / k, 1e-3), "min 0.5: crosshair scale %.2f (want 42 / %.3f = %.2f)", *xhair, k, 42.0 / k);
    setXhairMin(0.9f);
    RunFovBlock(kEffect, float(60 * kDeg));
    CHECK(Near(*xhair, 42.0 * 0.9, 1e-4), "min 0.9: crosshair scale %.2f (want %.2f)", *xhair, 42.0 * 0.9);
    RunFovBlock(kEffect, float(15 * kDeg));
    CHECK(*xhair == 42.0, "crosshair scale back to %.1f while scoped", *xhair);
    setXhairMin(1.0f);
    setFov(50.0f);
    RunFovBlock(kEffect, float(60 * kDeg));
    const double k50 = std::tan(25 * kDeg) / std::tan(30 * kDeg);
    CHECK(Near(*xhair, 42.0 / k50, 1e-3), "FOV 50: crosshair grows to %.2f (want %.2f)", *xhair, 42.0 / k50);
    setFov(75.0f);
    float huge = RunFovBlock(kCamera, float(170 * kDeg));
    CHECK(Near(huge / kDeg, 170.0), "170 widened past 175 is clamped: %.2f deg", huge / kDeg);

    printf("16:9 screen, default FOV: same as the game\n");
    setFov(0.0f);
    setViewport(g_ren, 0, 0, 1920, 1080);
    // the game's own formula: 2 * atan(tan(fov / 2) * 1.7777778 / 1.3333334), aspect 0.5625
    const double game169 = 2.0 * std::atan(std::tan(30 * kDeg) * 1.7777777910232544 / 1.3333333730697632) / kDeg;
    cam = RunFovBlock(kCamera, float(60 * kDeg), &aspect);
    CHECK(Near(cam / kDeg, game169, 1e-3) && aspect == 0.5625f, "camera: 60 -> %.3f deg (game %.3f), aspect %.4f",
          cam / kDeg, game169, aspect);

    printf("4:3 screen\n");
    setViewport(g_ren, 0, 0, 1024, 768);
    cam = RunFovBlock(kCamera, float(60 * kDeg), &aspect);
    CHECK(Near(cam / kDeg, 60.0, 1e-3) && Near(aspect, 0.75, 1e-6), "camera: 60 -> %.3f deg, aspect %.2f (unchanged)",
          cam / kDeg, aspect);
    CHECK(Near(*basis, 4.0 / 3.0, 1e-9), "grass basis back to 4/3");
    CHECK(RunDrop() == 0.75f, "bullet-drop markers keep 0.75 (%.4f)", g_x87Out);

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
    auto init = reinterpret_cast<InitFn>(Export("?Init@Renderer@@QAE_NI_NIII00@Z"));
    uint32_t ok = init(g_ren, 0, 0, 666, 666, 66, 0, 0);
    CHECK((ok & 0xff) == 1 && g_initW == 3440 && g_initH == 1440 && g_initHz >= 50 && g_initHz <= 255,
          "666x666@66 -> %ux%u@%u", g_initW, g_initH, g_initHz);

    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
