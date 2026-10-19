// Minimal x86 detour and import-table patching.
#include "dh2013ws.h"
#include <cstring>

namespace hook {

static bool Unprotect(void* p, size_t n, DWORD* old) {
    return VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, old) != 0;
}

static void WriteJmp(uint8_t* at, const void* dest) {
    at[0] = 0xE9;
    *reinterpret_cast<int32_t*>(at + 1) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(dest) -
                                                               reinterpret_cast<uintptr_t>(at) - 5);
}

void* Detour(uintptr_t target, void* detour, size_t len, const uint8_t* expect) {
    auto* t = reinterpret_cast<uint8_t*>(target);
    if (len < 5 || std::memcmp(t, expect, len) != 0) {
        Log("hook: bytes at %08X differ, not hooked", static_cast<unsigned>(target));
        return nullptr;
    }
    auto* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;
    std::memcpy(tramp, t, len);
    WriteJmp(tramp + len, t + len);
    DWORD old;
    if (!Unprotect(t, len, &old)) return nullptr;
    WriteJmp(t, detour);
    for (size_t i = 5; i < len; i++) t[i] = 0x90;
    VirtualProtect(t, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, len);
    return tramp;
}

bool Jump(uintptr_t target, void* dest, size_t expectLen, const uint8_t* expect) {
    auto* t = reinterpret_cast<uint8_t*>(target);
    if (expectLen < 5 || std::memcmp(t, expect, expectLen) != 0) {
        Log("hook: bytes at %08X differ, not replaced", static_cast<unsigned>(target));
        return false;
    }
    DWORD old;
    if (!Unprotect(t, 5, &old)) return false;
    WriteJmp(t, dest);
    VirtualProtect(t, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, 5);
    return true;
}

bool WritePtr(void* where, void* value) {
    DWORD old;
    if (!Unprotect(where, sizeof(void*), &old)) return false;
    *static_cast<void**>(where) = value;
    VirtualProtect(where, sizeof(void*), old, &old);
    return true;
}

void* PatchImport(uintptr_t module, const char* dll, const char* func, void* repl) {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(module + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(module + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp(reinterpret_cast<const char*>(module + imp->Name), dll) != 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(module + imp->OriginalFirstThunk);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA32*>(module + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(module + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), func) != 0) continue;
            void* prev = reinterpret_cast<void*>(iat->u1.Function);
            if (!WritePtr(&iat->u1.Function, repl)) return nullptr;
            return prev;
        }
    }
    Log("hook: import %s!%s not found", dll, func);
    return nullptr;
}

}  // namespace hook
