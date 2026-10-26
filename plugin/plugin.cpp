// GTA III 1.0 Weapon Loader - ASI plugin entry point and hooks.
// Hooks (all verified in gta3.exe; see gta3_addrs.hpp):
//   call sites : CWeaponInfo::Initialise (in InitialiseWeapons), CWeapon::UpdateWeapons (per frame),
//                C_PcSave::SaveSlot (menu save), GenericLoad (load)
//   detours    : CPickups::WeaponForModel, CPed::GiveWeapon, cDMAudio::PlayOneShot, CWeapon::Fire  (7-byte prologues)
// If ANY check fails nothing is patched and the game runs exactly as vanilla.
#include "runtime.hpp"

using namespace wl;
using namespace wl::eng;
namespace {

int (WL_CDECL* origWeaponForModel)(int) = nullptr;
bool (WL_THISCALL* origFire)(void*, void*, void*) = nullptr;
void (WL_THISCALL* origPlayOneShot)(void*, int, unsigned short, float) = nullptr;

// ---- pickups: a custom pickup model maps to its slot; GiveWeapon from CPickup::Update then installs the variant ------------
int WL_CDECL hkWeaponForModel(int model) {
    auto it = rt::pickupDef.find(model);
    if (it != rt::pickupDef.end()) { rt::pending = it->second; return it->second->slot; }
    rt::pending = nullptr;
    return origWeaponForModel(model);
}
void* WL_FASTCALL hkGiveWeapon(void* ped, void* /*edx*/, int type, unsigned ammo) {
    void* ra = WL_RETADDR();
    const WeaponDef* variant = nullptr;
    if (rt::state && ped == player() && rt::inPickupUpdate(ra)) {
        variant = variantForPickup(*rt::state, *rt::reg, type, rt::pending);   // see core/variant_logic.hpp (host-tested)
        rt::pending = nullptr;
    }
    void* r = rt::origGive(ped, type, ammo);     // the engine's own inventory/ammo logic runs first (unchanged semantics)
    if (variant) rt::occupySlot(*variant);
    return r;
}

// ---- audio: swap the engine's shot / reload one-shots for the player's custom OGG -----------------------------------------------
void WL_FASTCALL hkPlayOneShot(void* self, void* /*edx*/, int entity, unsigned short sound, float vol) {
    if ((sound == gta3::SOUND_WEAPON_SHOT_FIRED || sound == gta3::SOUND_WEAPON_RELOAD) && rt::state && rt::mixerOk) {
        void* ped = player();
        if (ped && entity == at<int>(u32(ped) + gta3::ped_audioEntity)) {
            int slot = currentSlot(ped);
            if (slot >= 0 && slot < SLOT_COUNT) {
                const WeaponDef& d = rt::state->active(slot); const int which = sound == gta3::SOUND_WEAPON_SHOT_FIRED ? 0 : 1;
                if (!d.isOriginal && rt::sound(d, which)) { rt::playSound(d, which); return; }     // custom sound replaces the stock one
            }
        }
    }
    origPlayOneShot(self, entity, sound, vol);
}
// GTA III plays nothing when the trigger is pulled on an empty weapon; Fire() simply returns false. We add the click.
bool WL_FASTCALL hkFire(void* weapon, void* /*edx*/, void* shooter, void* src) {
    bool r = origFire(weapon, shooter, src);
    if (!r && rt::state && rt::mixerOk && shooter && shooter == player()) {
        int type = at<int>(u32(weapon) + gta3::weapon::type), st = at<int>(u32(weapon) + gta3::weapon::state), total = at<int>(u32(weapon) + gta3::weapon::totalAmmo);
        if (type >= 2 && type <= 11 && total <= 0 && (st <= 1 || st == 3 /*READY, FIRING, OUT_OF_AMMO*/)) {
            uint32_t now = nowMs();
            if (now - rt::lastEmptyMs > 400) { rt::lastEmptyMs = now; const WeaponDef& d = rt::state->active(type); if (!d.isOriginal) rt::playSound(d, 2); }
        }
    }
    return r;
}

// ---- init / frame -------------------------------------------------------------------------------------------------------------
void WL_CDECL hkWeaponInfoInit() { fn<void(WL_CDECL*)()>(gta3::CWeaponInfo_Initialise)(); rt::onWeaponInit(); }
void WL_CDECL hkUpdateWeapons() { fn<void(WL_CDECL*)()>(gta3::CWeapon_UpdateWeapons)(); rt::tick(); }

// ---- persistence --------------------------------------------------------------------------------------------------------------
std::string cstr(uint32_t addr) {
    const char* p = reinterpret_cast<const char*>(static_cast<uintptr_t>(addr)); size_t n = 0; while (n < 259 && p[n]) ++n; return std::string(p, n);
}
char WL_FASTCALL hkSaveSlot(void* self, void* /*edx*/, int slot) {
    char ok = fn<char(WL_THISCALL*)(void*, int)>(gta3::C_PcSave_SaveSlot)(self, slot);
    if (ok && rt::state) rt::writeSidecarAfterSave(cstr(gta3::saveFileName));   // the .b file is closed and final here
    return ok;
}
char WL_CDECL hkGenericLoad() {
    if (rt::state) rt::preApplyFromSidecar(cstr(gta3::loadFileName));
    char ok = fn<char(WL_CDECL*)()>(gta3::GenericLoad)();
    if (rt::state) { rt::loadedSave = ok != 0; rt::verifyAfterLoad(ok != 0); }
    return ok;
}

bool versionOk() {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(0x400000);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(0x400000 + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != gta3::kPeTimeDateStamp) { logf("PE timestamp %08X is not GTA III 1.0", nt->FileHeader.TimeDateStamp); return false; }
    return sameBytes(gta3::CWeaponInfo_GetWeaponInfo, gta3::sig_GetWeaponInfo, sizeof gta3::sig_GetWeaponInfo);
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    g_log = std::fopen("weaponloader.log", "w");
    logf("GTA III Weapon Loader (models, icons, OGG audio, animations, save persistence)");
    if (!versionOk()) { logf("unsupported executable; loader disabled (no memory was modified)"); return TRUE; }
    rt::readIni();
    if (GetPrivateProfileIntA("General", "Enabled", 1, ".\\weaponloader\\loader.ini") == 0) { logf("disabled in loader.ini"); return TRUE; }

    // 1. validate EVERYTHING before touching memory
    using namespace gta3;
    bool ok = callSiteOk(site_InitialiseWeapons_callInitialise, CWeaponInfo_Initialise) && callSiteOk(site_GameProcess_callUpdateWeapons, CWeapon_UpdateWeapons) &&
              callSiteOk(site_SaveSlot_call, C_PcSave_SaveSlot) && callSiteOk(site_GenericLoad_call, GenericLoad) &&
              sameBytes(CPickups_WeaponForModel, sig_WeaponForModel, 7) && sameBytes(CPed_GiveWeapon, sig_GiveWeapon, 7) &&
              sameBytes(cDMAudio_PlayOneShot, sig_PlayOneShot, 7) && sameBytes(CWeapon_Fire, sig_Fire, 7);
    if (!ok) { logf("signature check failed; loader disabled (no memory was modified)"); return TRUE; }

    // 2. apply
    origWeaponForModel = reinterpret_cast<int (WL_CDECL*)(int)>(detour7(CPickups_WeaponForModel, sig_WeaponForModel, (void*)&hkWeaponForModel));
    rt::origGive = reinterpret_cast<void* (WL_THISCALL*)(void*, int, unsigned)>(detour7(CPed_GiveWeapon, sig_GiveWeapon, (void*)&hkGiveWeapon));
    origPlayOneShot = reinterpret_cast<void (WL_THISCALL*)(void*, int, unsigned short, float)>(detour7(cDMAudio_PlayOneShot, sig_PlayOneShot, (void*)&hkPlayOneShot));
    origFire = reinterpret_cast<bool (WL_THISCALL*)(void*, void*, void*)>(detour7(CWeapon_Fire, sig_Fire, (void*)&hkFire));
    bool all = origWeaponForModel && rt::origGive && origPlayOneShot && origFire &&
               patchCall(site_InitialiseWeapons_callInitialise, (void*)&hkWeaponInfoInit) && patchCall(site_GameProcess_callUpdateWeapons, (void*)&hkUpdateWeapons) &&
               patchCall(site_SaveSlot_call, (void*)&hkSaveSlot) && patchCall(site_GenericLoad_call, (void*)&hkGenericLoad);
    logf(all ? "all hooks installed" : "hook install reported a failure; see lines above");
    return TRUE;
}
