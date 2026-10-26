// Thin typed wrappers over verified GTA III 1.0 engine functions plus the two patching primitives the loader uses:
//   * patchCall  - replace an `E8 rel32` call site after checking its current target
//   * detour7    - redirect a function whose first 7 bytes are a clean, position-independent instruction run
// Nothing is patched unless the bytes at the site equal what was read from the supplied gta3.exe.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../core/image_util.hpp"
#include "gta3_addrs.hpp"

#if defined(_MSC_VER)
#include <intrin.h>
#define WL_CDECL __cdecl
#define WL_THISCALL __thiscall
#define WL_FASTCALL __fastcall
#define WL_RETADDR() _ReturnAddress()
#else
#define WL_CDECL __attribute__((cdecl))
#define WL_THISCALL __attribute__((thiscall))
#define WL_FASTCALL __attribute__((fastcall))
#define WL_RETADDR() __builtin_return_address(0)
#endif

namespace wl { namespace eng {
inline FILE* g_log = nullptr;
inline void logf(const char* fmt, ...) {
    if (!g_log) return;
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fputc('\n', g_log); fflush(g_log);
}
template <class T> inline T& at(uint32_t addr) { return *reinterpret_cast<T*>(static_cast<uintptr_t>(addr)); }
template <class T> inline T fn(uint32_t addr) { return reinterpret_cast<T>(static_cast<uintptr_t>(addr)); }
inline uint32_t u32(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
inline bool fileExists(const std::string& p) { DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

// ---- patching ---------------------------------------------------------------------------------------------------
inline bool sameBytes(uint32_t addr, const uint8_t* b, size_t n) { return std::memcmp(reinterpret_cast<void*>(static_cast<uintptr_t>(addr)), b, n) == 0; }
inline bool callSiteOk(uint32_t site, uint32_t expectedTarget) {
    auto* p = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(site));
    if (p[0] != 0xE8) { logf("call site %08X: not a call (opcode %02X)", site, p[0]); return false; }
    uint32_t cur = site + 5 + *reinterpret_cast<int32_t*>(p + 1);
    if (cur != expectedTarget) { logf("call site %08X: target %08X != expected %08X", site, cur, expectedTarget); return false; }
    return true;
}
inline bool patchCall(uint32_t site, void* newTarget) {
    auto* p = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(site)); DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *reinterpret_cast<int32_t*>(p + 1) = static_cast<int32_t>(u32(newTarget) - (site + 5));
    VirtualProtect(p, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), p, 5); return true;
}
// Returns a trampoline that runs the original function, or nullptr (nothing modified) if the bytes do not match.
inline void* detour7(uint32_t site, const uint8_t* expect7, void* hook) {
    if (!sameBytes(site, expect7, 7)) { logf("detour %08X: prologue bytes differ, not patched", site); return nullptr; }
    auto* tr = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tr) return nullptr;
    auto* p = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(site));
    std::memcpy(tr, p, 7); tr[7] = 0xE9; *reinterpret_cast<int32_t*>(tr + 8) = static_cast<int32_t>((site + 7) - (u32(tr) + 12));
    DWORD old; if (!VirtualProtect(p, 7, PAGE_EXECUTE_READWRITE, &old)) return nullptr;
    p[0] = 0xE9; *reinterpret_cast<int32_t*>(p + 1) = static_cast<int32_t>(u32(hook) - (site + 5)); p[5] = 0x90; p[6] = 0x90;
    VirtualProtect(p, 7, old, &old); FlushInstructionCache(GetCurrentProcess(), p, 7);
    return tr;
}

// ---- game objects -------------------------------------------------------------------------------------------------
inline void* player() { return fn<void* (WL_CDECL*)()>(gta3::FindPlayerPed)(); }
inline uint32_t nowMs() { return at<uint32_t>(gta3::CTimer_ms); }
inline uint32_t infoAddr(int slot) { return gta3::aWeaponInfo + (uint32_t)slot * gta3::kWeaponInfoSize; }
inline uint32_t pedWeapon(void* ped, int slot) { return u32(ped) + gta3::ped_weapons + (uint32_t)slot * gta3::kWeaponSize; }
inline bool ownsSlot(void* ped, int slot) { return at<int>(pedWeapon(ped, slot) + gta3::weapon::type) == slot; }
inline int currentSlot(void* ped) { return at<int8_t>(u32(ped) + gta3::ped_currentSlot); }
inline void makeChangesForNewWeapon(void* ped, int slot) { fn<void(WL_THISCALL*)(void*, char)>(gta3::CPlayerPed_MakeChangesForNewWeapon)(ped, (char)slot); }

// ---- models / txd / streaming ----------------------------------------------------------------------------------------
inline void*& modelInfo(int id) { return at<void*>(gta3::ms_modelInfoPtrs + 4u * (uint32_t)id); }
inline bool modelSlotFree(int id) { return id >= 0 && id < gta3::kModelInfoSize && modelInfo(id) == nullptr; }
inline int simpleStoreCount() { return at<int>(gta3::simpleModelStoreCount); }
inline int findTxdSlot(const char* n) { return (int16_t)fn<int(WL_CDECL*)(const char*)>(gta3::CTxdStore_FindTxdSlot)(n); }
inline int addTxdSlot(const char* n) { return (int16_t)fn<int(WL_CDECL*)(const char*)>(gta3::CTxdStore_AddTxdSlot)(n); }
// LoadTxd retries forever when the file cannot be opened (verified @0x5276C1), so existence is checked first.
inline bool loadTxd(int slot, const std::string& path) {
    if (!fileExists(path)) return false;
    return (fn<char(WL_CDECL*)(int, const char*)>(gta3::CTxdStore_LoadTxd)(slot, path.c_str())) != 0;
}
inline void markStreamLoaded(int streamId) {
    auto* s = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(gta3::CStreaming_ms_aInfoForModel + (uint32_t)streamId * gta3::kStreamInfoSize));
    s[8] = gta3::STREAMSTATE_LOADED; s[9] = gta3::STREAMFLAGS_DONT_REMOVE;   // never enters the removable list
}
inline bool loadDffInto(int modelId, int txdSlot, const std::string& path) {
    if (!fileExists(path)) return false;
    fn<void(WL_CDECL*)()>(gta3::CTxdStore_PushCurrentTxd)();
    fn<void(WL_CDECL*)(int)>(gta3::CTxdStore_SetCurrentTxd)(txdSlot);
    void* st = fn<void* (WL_CDECL*)(int, int, const char*)>(gta3::RwStreamOpen)(2 /*file*/, 1 /*read*/, path.c_str());
    bool ok = false;
    if (st) {
        ok = fn<char(WL_CDECL*)(void*, unsigned)>(gta3::CFileLoader_LoadAtomicFile_stream)(st, (unsigned)modelId) != 0;
        fn<int(WL_CDECL*)(void*, void*)>(gta3::RwStreamClose)(st, nullptr);
    }
    // LoadAtomicFile reports success even when the file has no clump chunk (verified @0x476944), so check the result itself:
    // CSimpleModelInfo::m_atomics[0] lives at +0x30 (SetAtomic @0x517950 stores to [this+idx*4+0x30]).
    if (ok && !modelInfo(modelId)) ok = false;
    if (ok && at<uint32_t>(u32(modelInfo(modelId)) + 0x30) == 0) ok = false;
    fn<void(WL_CDECL*)()>(gta3::CTxdStore_PopCurrentTxd)();
    return ok;
}
inline bool registerObject(const std::string& ideLine) {
    std::vector<char> b(ideLine.begin(), ideLine.end()); b.push_back(0);
    fn<void(WL_CDECL*)(const char*)>(gta3::CFileLoader_LoadObject)(b.data()); return true;
}
inline bool modelNameTaken(const std::string& name) { int id = -1; return fn<void* (WL_CDECL*)(const char*, int*)>(gta3::CModelInfo_GetModelInfoByName)(name.c_str(), &id) != nullptr; }

// ---- RenderWare texture from RGBA ----------------------------------------------------------------------------------------
inline void* makeTexture(const Image& src, const char* name) {
    using namespace gta3;
    auto create = fn<void* (WL_CDECL*)(int, int, int)>(RwImageCreate);
    auto alloc = fn<void* (WL_CDECL*)(void*)>(RwImageAllocatePixels);
    auto destroy = fn<int(WL_CDECL*)(void*)>(RwImageDestroy);
    auto findFmt = fn<void* (WL_CDECL*)(void*, int, int*, int*, int*, int*)>(RwImageFindRasterFormat);
    auto fill = [&](const Image& im) -> void* {
        void* img = create(im.w, im.h, 32); if (!img) return nullptr;
        if (!alloc(img)) { destroy(img); return nullptr; }
        auto* px = at<uint8_t*>(u32(img) + rwimg::pixels); int stride = at<int>(u32(img) + rwimg::stride);
        for (int y = 0; y < im.h; ++y) std::memcpy(px + (size_t)y * stride, &im.rgba[(size_t)y * im.w * 4], (size_t)im.w * 4);
        return img;
    };
    void* img = fill(src); if (!img) return nullptr;
    int w = 0, h = 0, d = 0, fmt = 0;
    if (!findFmt(img, 4 /*rwRASTERTYPETEXTURE*/, &w, &h, &d, &fmt)) { destroy(img); return nullptr; }
    if (w != src.w || h != src.h) {                       // renderer wants other dimensions (power of two): resample ourselves
        destroy(img); Image r = resizeBilinear(src, w, h); img = fill(r); if (!img) return nullptr;
    }
    void* raster = fn<void* (WL_CDECL*)(int, int, int, int)>(RwRasterCreate)(w, h, d, fmt);
    void* tex = nullptr;
    if (raster && fn<void* (WL_CDECL*)(void*, void*)>(RwRasterSetFromImage)(raster, img)) {
        tex = fn<void* (WL_CDECL*)(void*)>(RwTextureCreate)(raster);
        if (tex) { fn<void* (WL_CDECL*)(void*, const char*)>(RwTextureSetName)(tex, name); at<uint8_t>(u32(tex) + 0x50) = 2; /* rwFILTERLINEAR */ }
    }
    destroy(img); return tex;
}

// ---- animations ------------------------------------------------------------------------------------------------------
inline void* stdAssoc(const char* name) { return fn<void* (WL_CDECL*)(int, const char*)>(gta3::CAnimManager_GetAnimAssociationByName)(0 /*ASSOCGRP_STD*/, name); }
inline int stdAnimId(const char* name) { void* a = stdAssoc(name); return a ? at<int>(u32(a) + gta3::assoc::animId) : -1; }
}} // namespace wl::eng
