// Cabela's Dangerous Hunts 2013 (demo) widescreen mod: dinput8.dll proxy, config, log, startup.
// CloakNTEngine.dll (the engine) imports dinput8.dll, so this DLL initialises before the engine runs any code.
#include "dh2013ws.h"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

Config g_cfg;
char g_iniPath[MAX_PATH];
uintptr_t g_engine;

static char g_logPath[MAX_PATH];
static CRITICAL_SECTION g_logLock;
static bool g_installed;

// Steam demo build (app 225580) of CloakNTEngine.dll (PE TimeDateStamp). Patch offsets are only valid for it.
static const DWORD kEngineStamp = 1353949478;

void Log(const char* fmt, ...) {
    if (!g_cfg.log || !g_logPath[0]) return;
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, sizeof line, _TRUNCATE, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_logLock);
    if (FILE* f = fopen(g_logPath, "a")) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(f, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
        fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
}

// ---- config ----------------------------------------------------------------
static int IniInt(const char* sec, const char* key, int def) {
    return static_cast<int>(GetPrivateProfileIntA(sec, key, def, g_iniPath));
}
static float IniFloat(const char* sec, const char* key, float def) {
    char buf[64];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof buf, g_iniPath);
    return buf[0] ? static_cast<float>(atof(buf)) : def;
}

static void LoadConfig() {
    Config& c = g_cfg;
    c.allModes = IniInt("Display", "AllModes", 1) != 0;
    c.minRefresh = IniInt("Display", "MinRefresh", 50);
    c.width = IniInt("Display", "Width", 0);
    c.height = IniInt("Display", "Height", 0);
    c.refresh = IniInt("Display", "Refresh", 0);

    c.fixAspect = IniInt("Camera", "FixAspect", 1) != 0;
    c.fov = IniFloat("Camera", "FOV", 0.0f);
    c.zoomBlend = IniFloat("Camera", "ZoomBlend", 2.0f);
    c.keyFovUp = IniInt("Camera", "KeyFOVUp", VK_ADD);
    c.keyFovDown = IniInt("Camera", "KeyFOVDown", VK_SUBTRACT);
    c.keyFovReset = IniInt("Camera", "KeyFOVReset", VK_MULTIPLY);
    c.fovStep = IniFloat("Camera", "FOVStep", 5.0f);

    c.log = IniInt("Debug", "Log", 1) != 0;
}

void SaveFovToIni() {
    char buf[32];
    sprintf_s(buf, "%.1f", g_cfg.fov);
    WritePrivateProfileStringA("Camera", "FOV", buf, g_iniPath);
}

// ---- startup -------------------------------------------------------------------
static DWORD Stamp(uintptr_t base) {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    return reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew)->FileHeader.TimeDateStamp;
}

// Runs from DllMain (the engine is mapped and relocated but not initialised yet) or, failing that, on the
// first DirectInput8Create call.
static void TryInstall() {
    if (g_installed) return;
    g_engine = reinterpret_cast<uintptr_t>(GetModuleHandleA("CloakNTEngine.dll"));
    if (!g_engine) return;
    g_installed = true;
    if (Stamp(g_engine) != kEngineStamp) {
        Log("unsupported CloakNTEngine.dll (timestamp %u, expected %u): mod disabled", Stamp(g_engine), kEngineStamp);
        return;
    }
    Log("CloakNTEngine.dll at %08X", static_cast<unsigned>(g_engine));
    InstallPatches();
}

// ---- dinput8 proxy -----------------------------------------------------------
static HMODULE g_real;
static FARPROC g_procs[6];
static const char* const kProcNames[6] = {"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject",
                                          "DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick"};

static void LoadReal() {
    if (g_real) return;
    char path[MAX_PATH];
    char chain[MAX_PATH] = "";
    GetPrivateProfileStringA("Proxy", "Chain", "", chain, sizeof chain, g_iniPath);
    if (chain[0]) {
        g_real = LoadLibraryA(chain);
    }
    if (!g_real) {
        GetSystemDirectoryA(path, MAX_PATH);
        strcat_s(path, "\\dinput8.dll");
        g_real = LoadLibraryA(path);
    }
    if (!g_real) {
        Log("FATAL: could not load the system dinput8.dll");
        return;
    }
    for (int i = 0; i < 6; i++) g_procs[i] = GetProcAddress(g_real, kProcNames[i]);
}

extern "C" {
FARPROC g_pDirectInput8Create, g_pDllCanUnloadNow, g_pDllGetClassObject, g_pDllRegisterServer,
    g_pDllUnregisterServer, g_pGetdfDIJoystick;

static void __stdcall ResolveProcs() {
    TryInstall();
    LoadReal();
    g_pDirectInput8Create = g_procs[0];
    g_pDllCanUnloadNow = g_procs[1];
    g_pDllGetClassObject = g_procs[2];
    g_pDllRegisterServer = g_procs[3];
    g_pDllUnregisterServer = g_procs[4];
    g_pGetdfDIJoystick = g_procs[5];
}

// Forwarders keep the caller's stack untouched and jump to the real export.
#define PROXY(name)                                \
    __declspec(naked) void Proxy_##name() {        \
        __asm { pushad }                           \
        __asm { call ResolveProcs }                \
        __asm { popad }                            \
        __asm { jmp dword ptr [g_p##name] }        \
    }
PROXY(DirectInput8Create)
PROXY(DllCanUnloadNow)
PROXY(DllGetClassObject)
PROXY(DllRegisterServer)
PROXY(DllUnregisterServer)
PROXY(GetdfDIJoystick)
}

static void Startup(HMODULE self) {
    InitializeCriticalSection(&g_logLock);
    char dir[MAX_PATH];
    GetModuleFileNameA(self, dir, MAX_PATH);
    if (char* slash = strrchr(dir, '\\')) slash[1] = 0;
    sprintf_s(g_iniPath, "%sdh2013ws.ini", dir);
    sprintf_s(g_logPath, "%sdh2013ws.log", dir);
    LoadConfig();
    DeleteFileA(g_logPath);
    Log("dh2013ws %s %s (module %p)", __DATE__, __TIME__, self);
    TryInstall();
    if (!g_installed) Log("CloakNTEngine.dll not loaded yet: patching on first DirectInput8Create");
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        Startup(mod);
    }
    return TRUE;
}
