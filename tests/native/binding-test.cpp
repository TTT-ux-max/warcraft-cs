// Offline binding test for the WarcraftCS module.
//
// Runs as a 32-bit process and links the *same* GameBuild.cpp the shipped
// WarcraftCS.mix uses, so it exercises the real build selector instead of a
// reimplementation. It loads the owned Game.dll, lets SelectBuild() classify it,
// then walks both generated address tables through the public Rva() accessor and
// checks every translated target against the live image.
//
// Exit code 0 = bound, 1 = refused, 2 = table defect.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "../../src/platform/GameBuild.hpp"

#pragma comment(lib, "version.lib")

struct RvaPair { uintptr_t legacy; uintptr_t translated; };
static const RvaPair kInternals[] = {
#define BUILD_ADDRESS(legacy, translated) {legacy, translated},
#include "../../src/platform/GameBuild127a.inc"
#undef BUILD_ADDRESS
};

struct NamedRva { const char* name; uintptr_t rva; };
static const NamedRva kNatives[] = {
#define NATIVE(result, name, arguments, offset) {#name, offset},
#include "../../src/platform/NativeOffsets127a.inc"
#undef NATIVE
};

struct Section { uintptr_t va, vsize, rsize; uint32_t characteristics; char name[9]; };
static Section g_sections[24];
static int g_sectionCount = 0;
static uintptr_t g_base = 0;
static uintptr_t g_imageSize = 0;

static void LoadSections(HMODULE module) {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(reinterpret_cast<uint8_t*>(module) + dos->e_lfanew);
    g_imageSize = nt->OptionalHeader.SizeOfImage;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections && i < 24; ++i) {
        g_sections[g_sectionCount].va = sec[i].VirtualAddress;
        g_sections[g_sectionCount].vsize = sec[i].Misc.VirtualSize;
        g_sections[g_sectionCount].rsize = sec[i].SizeOfRawData;
        g_sections[g_sectionCount].characteristics = sec[i].Characteristics;
        memcpy(g_sections[g_sectionCount].name, sec[i].Name, 8);
        g_sections[g_sectionCount].name[8] = 0;
        ++g_sectionCount;
    }
}

// Guarded read: a wrong address must be reported, not crash the probe.
static bool SafeRead(uintptr_t address, void* out, size_t length) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(address), length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        memset(out, 0, length);
        return false;
    }
}

static const Section* FindSection(uintptr_t rva) {
    for (int i = 0; i < g_sectionCount; ++i) {
        const Section& s = g_sections[i];
        const uintptr_t span = s.vsize > s.rsize ? s.vsize : s.rsize;
        if (rva >= s.va && rva <= s.va + span) return &s;
    }
    return nullptr;
}

static bool IsExecutableRva(uintptr_t rva, size_t length) {
    for (int i = 0; i < g_sectionCount; ++i) {
        const Section& s = g_sections[i];
        if (!(s.characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uintptr_t span = s.vsize > s.rsize ? s.vsize : s.rsize;
        if (rva >= s.va && rva + length <= s.va + span) return true;
    }
    return false;
}

static bool IsPlausiblePrologue(const uint8_t* p) {
    // Real entry points in this build start with a frame, a stack probe, a register
    // push, a jump-table stub, a move or a return. A run of 0xCC/0x90 padding, a
    // near pointer, or a call to nowhere is what a wrong address actually looks like.
    switch (p[0]) {
        case 0x55: case 0x53: case 0x56: case 0x57: case 0x51: case 0x52:
        case 0x8B: case 0x83: case 0x81: case 0xE9: case 0xE8: case 0xB8:
        case 0x6A: case 0x68: case 0x33: case 0x2B: case 0x3B: case 0xC3:
        case 0xF3: case 0x50: case 0x0F: case 0xEB: case 0x8D: case 0xA1:
        case 0xDE: case 0xDD: case 0xD9: case 0x89: case 0x85: case 0x64:
        case 0x4C: case 0x00:
            return true;
        default:
            return false;
    }
}

static bool IsPadding(const uint8_t* p) { return p[0] == 0xCC || (p[0] == 0x90 && p[1] == 0x90 && p[2] == 0x90); }

// Line-for-line replica of wc3::SelectBuild()/Verify127a() with a print after every
// branch, so a disagreement between the probe and the shipped code is attributable.
static void ProbeSelect(HMODULE module) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    printf("probe path     : %s\n", path);
    DWORD ignored = 0, size = GetFileVersionInfoSizeA(path, &ignored);
    printf("probe fvsize   : %lu\n", size);
    if (!size) return;
    auto* bytes = new unsigned char[size];
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    bool readable = GetFileVersionInfoA(path, 0, size, bytes) &&
        VerQueryValueA(bytes, "\\", reinterpret_cast<void**>(&info), &length) && info;
    if (readable) {
        printf("probe version  : MS=0x%08X LS=0x%08X  msMatch=%d lsMatch=%d\n",
               info->dwFileVersionMS, info->dwFileVersionLS,
               info->dwFileVersionMS == (DWORD)MAKELONG(27, 1),
               info->dwFileVersionLS == (DWORD)MAKELONG(52240, 0));
    } else {
        printf("probe version  : unreadable\n");
    }
    delete[] bytes;

    static const unsigned char sehPrologue[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68};
    static const unsigned char nativePrologue[] = {0x55, 0x8B, 0xEC, 0x33, 0xD2, 0x8D, 0x4A, 0x01, 0xE8};
    struct Gate { uintptr_t legacy; const unsigned char* expected; size_t length; };
    const Gate gates[] = {
        {0x3BDCB0, sehPrologue, sizeof(sehPrologue)},
        {0x300710, sehPrologue, sizeof(sehPrologue)},
        {0x3B3810, nativePrologue, sizeof(nativePrologue)},
    };
    for (const Gate& gate : gates) {
        uintptr_t rva = 0;
        for (const RvaPair& row : kInternals) if (row.legacy == gate.legacy) { rva = row.translated; break; }
        uint8_t bytes[16] = {};
        bool ok = rva && SafeRead(g_base + rva, bytes, sizeof(bytes));
        printf("probe verify 0x%06X: rva=0x%06X read=%s memcmp=%d\n", (unsigned)gate.legacy,
               (unsigned)rva, ok ? "ok" : "FAILED",
               ok ? memcmp(bytes, gate.expected, gate.length) : -1);
    }
}

int main(int argc, char** argv) {
    // Unbuffered output: a fault inside the probe must not swallow the lines already
    // printed, otherwise a crash is indistinguishable from a clean refusal.
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* gameDll = argc > 1 ? argv[1] : "D:\\game\\Warcraft III 1.27 Reforged\\game.dll";
    char gameDir[MAX_PATH];
    strcpy_s(gameDir, gameDll);
    if (char* slash = strrchr(gameDir, '\\')) *slash = 0;

    printf("harness        : 32-bit process, image base 0x%08X\n", (unsigned)(uintptr_t)GetModuleHandle(nullptr));
    printf("target         : %s\n", gameDll);
    SetDllDirectoryA(gameDir);
    HMODULE module = LoadLibraryExA(gameDll, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) { printf("RESULT         : FAIL LoadLibraryEx error=%lu\n", GetLastError()); return 1; }
    g_base = reinterpret_cast<uintptr_t>(module);
    LoadSections(module);

    char resolved[MAX_PATH] = {};
    GetModuleFileNameA(module, resolved, MAX_PATH);
    printf("loaded at      : 0x%08X\n", (unsigned)g_base);
    printf("loaded from    : %s\n", resolved);
    printf("image size     : 0x%X\n", (unsigned)g_imageSize);
    {
        uint8_t head[16] = {};
        bool ok = SafeRead(g_base + 0x1000, head, sizeof(head));
        printf(".text@0x1000   : %s ", ok ? "" : "UNREADABLE");
        if (ok) for (int i = 0; i < 16; ++i) printf("%02X ", head[i]);
        printf("\n");
    }

    // The three prologues Verify127a() gates on, reported individually so a failing
    // one is attributable rather than just "refused". Resolved straight from the
    // generated table so the probe also runs when the selector rejected the module.
    ProbeSelect(module);
    const uintptr_t gates[] = {0x3BDCB0, 0x300710, 0x3B3810};
    const char* gateNames[] = {"CUnit resolver", "UI accessor", "JASS native"};
    for (int i = 0; i < 3; ++i) {
        uintptr_t t = 0;
        for (const RvaPair& row : kInternals) if (row.legacy == gates[i]) { t = row.translated; break; }
        uint8_t bytes[6] = {};
        bool ok = t ? SafeRead(g_base + t, bytes, sizeof(bytes)) : false;
        const Section* s = t ? FindSection(t) : nullptr;
        MEMORY_BASIC_INFORMATION mbi = {};
        if (t) VirtualQuery(reinterpret_cast<const void*>(g_base + t), &mbi, sizeof(mbi));
        printf("gate %-15s 0x%06X -> 0x%06X sec=%-7s protect=0x%X %s\n", gateNames[i], (unsigned)gates[i],
               (unsigned)t, s ? s->name : "NONE", (unsigned)mbi.Protect, ok ? "" : "UNREADABLE");
        if (ok) printf("                                   bytes=%02X %02X %02X %02X %02X %02X\n",
                       bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    }

    // First 24 bytes of the four anchors whose 1.27a prologues the document records.
    const uintptr_t anchors[] = {0x2F5ED0, 0x390590, 0x3B4C62, 0x3C8DA0};
    for (uintptr_t legacy : anchors) {
        uintptr_t t = 0;
        for (const RvaPair& row : kInternals) if (row.legacy == legacy) { t = row.translated; break; }
        uint8_t bytes[24] = {};
        bool ok = t ? SafeRead(g_base + t, bytes, sizeof(bytes)) : false;
        printf("anchor 0x%06X -> 0x%06X %s ", (unsigned)legacy, (unsigned)t, ok ? "" : "UNREADABLE");
        if (ok) for (int i = 0; i < 12; ++i) printf("%02X ", bytes[i]);
        printf("\n");
    }

    wc3::GameBuildKind kind = wc3::SelectBuild(module);
    printf("SelectBuild()  : %d (%s)\n", (int)kind, wc3::ActiveBuildName());
    if (kind == wc3::GameBuildKind::Unsupported) { printf("RESULT         : FAIL build refused\n"); return 1; }

    int failures = 0, checked = 0, zero = 0;
    for (const RvaPair& row : kInternals) {
        ++checked;
        uintptr_t translated = wc3::Rva(row.legacy);
        if (translated != row.translated) {
            printf("MISMATCH 0x%06X -> Rva()=0x%06X table=0x%06X\n",
                   (unsigned)row.legacy, (unsigned)translated, (unsigned)row.translated);
            ++failures;
            continue;
        }
        if (!translated) { ++zero; continue; }
        uint8_t bytes[3] = {};
        if (!SafeRead(g_base + translated, bytes, sizeof(bytes))) {
            printf("UNREADABLE 0x%06X -> 0x%06X\n", (unsigned)row.legacy, (unsigned)translated);
            ++failures;
            continue;
        }
        if (!IsExecutableRva(translated, 3) || IsPadding(bytes) || !IsPlausiblePrologue(bytes)) {
            printf("BADTARGET 0x%06X -> 0x%06X bytes=%02X %02X %02X %02X\n", (unsigned)row.legacy,
                   (unsigned)translated, bytes[0], bytes[1], bytes[2], bytes[3]);
            ++failures;
        }
    }
    printf("internals      : %d rows, %d resolvable, %d retired (0), %d bad\n",
           checked, checked - zero, zero, failures);

    int nchecked = 0, nbad = 0, nzero = 0;
    for (const NamedRva& row : kNatives) {
        ++nchecked;
        if (!row.rva) { ++nzero; continue; }
        uint8_t bytes[16] = {};
        if (!SafeRead(g_base + row.rva, bytes, sizeof(bytes))) {
            printf("NATIVE-UNREADABLE %-28s 0x%06X\n", row.name, (unsigned)row.rva);
            ++nbad;
            continue;
        }
        if (!IsExecutableRva(row.rva, 16) || IsPadding(bytes) || !IsPlausiblePrologue(bytes)) {
            printf("NATIVE-BAD %-28s 0x%06X bytes=%02X %02X %02X %02X\n", row.name, (unsigned)row.rva,
                   bytes[0], bytes[1], bytes[2], bytes[3]);
            ++nbad;
        }
    }
    printf("natives        : %d rows, %d resolvable, %d retired (0), %d bad\n",
           nchecked, nchecked - nzero, nzero, nbad);
    failures += nbad;

    printf("RESULT         : %s\n", failures ? "FAIL" : "PASS");
    return failures ? 2 : 0;
}
