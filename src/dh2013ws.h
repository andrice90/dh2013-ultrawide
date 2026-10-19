// Cabela's Dangerous Hunts 2013 (demo) widescreen mod: shared declarations.
#pragma once
#include <windows.h>
#include <cstdint>

// ---- config (dh2013ws.ini next to dh_2013_demo.exe) -------------------------
struct Config {
    // [Display]
    bool allModes = true;  // list every display mode the monitor offers (the game whitelists up to 2560 wide)
    int minRefresh = 50;   // modes below this refresh rate stay hidden
    int width = 0;         // 0 = the game's own resolution setting; else forced at startup
    int height = 0;
    int refresh = 0;       // 0 = keep the game's refresh rate (or the highest for the forced size)
    // [Camera]
    bool fixAspect = true;   // real aspect ratio for the 3D view + Hor+ field of view from it
    float fov = 0.0f;        // 0 = game default; else horizontal FOV (degrees, 4:3 basis) of the normal view
    float zoomBlend = 2.0f;  // zoom factor at which scopes/binoculars get their original magnification back
    int keyFovUp = VK_ADD, keyFovDown = VK_SUBTRACT, keyFovReset = VK_MULTIPLY;
    float fovStep = 5.0f;
    // [UI]
    bool fixUI = true;         // menus and HUD on a canvas with the real screen shape (not stretched)
    bool crosshairFov = true;  // crosshair shrinks with the FOV setting so it covers the same spread
    // [Debug]
    bool log = true;
};

extern Config g_cfg;
extern char g_iniPath[MAX_PATH];
extern uintptr_t g_engine;  // CloakNTEngine.dll base

void Log(const char* fmt, ...);
void SaveFovToIni();

// ---- hooking ---------------------------------------------------------------
namespace hook {
// Detours `target` to `detour`. `len` = bytes of whole, position-independent instructions to relocate
// (>= 5); they must equal `expect`. Returns a trampoline that runs the original, or nullptr.
void* Detour(uintptr_t target, void* detour, size_t len, const uint8_t* expect);
// Writes a jump at `target` to `dest` (the bytes there must equal `expect`, >= 5 of them).
bool Jump(uintptr_t target, void* dest, size_t expectLen, const uint8_t* expect);
// Replaces an import of `module` (from `dll`) by name. Returns the previous pointer, or nullptr.
void* PatchImport(uintptr_t module, const char* dll, const char* func, void* repl);
// Writes a pointer into (possibly read-only) memory.
bool WritePtr(void* where, void* value);
}

// ---- features ----------------------------------------------------------
bool InstallPatches();  // display modes, aspect ratio, field of view
