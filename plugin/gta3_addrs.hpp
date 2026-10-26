// Addresses and layouts for GTA III PC 1.0 (gta3.exe, 2,383,872 bytes, PE TimeDateStamp 0x3CCECE14).
// Every entry was read out of the supplied gta3.exe (objdump disassembly / raw bytes) and named with the supplied
// gta3_1_0_kenking_.idb; struct layouts were cross-checked against the supplied re3 source (they match).
// Call sites and prologue signatures below were re-checked mechanically against the exe (see docs).
#pragma once
#include <cstdint>

namespace gta3 {
constexpr uint32_t kPeTimeDateStamp = 0x3CCECE14;

// ---- CWeaponInfo ----------------------------------------------------------------------------------------------
constexpr uint32_t aWeaponInfo = 0x6503EC;              // GetWeaponInfo @0x564FD0: imul 0x54, add 0x6503EC
constexpr uint32_t kWeaponInfoSize = 0x54;
constexpr uint32_t CWeaponInfo_GetWeaponInfo = 0x564FD0;
namespace winfo {   // LoadWeaponData stores (0x564FE0), identical to re3 CWeaponInfo
constexpr uint32_t range = 0x04, fireRate = 0x08, reload = 0x0C, clipAmmo = 0x10, damage = 0x14,
                   anim1 = 0x34, anim2 = 0x38, loopStart = 0x3C, loopEnd = 0x40, frameFire = 0x44, frame2Fire = 0x48,
                   modelId = 0x4C, flags = 0x50;
}
constexpr int kNumWeaponSlots = 13;                     // ProcessWeaponSwitch loops 0..12

// ---- CPed / CWeapon -------------------------------------------------------------------------------------------
constexpr uint32_t ped_weapons = 0x35C, kWeaponSize = 0x18;
namespace weapon { constexpr uint32_t type = 0x00, state = 0x04, ammoInClip = 0x08, totalAmmo = 0x0C; }
constexpr uint32_t ped_audioEntity = 0x64;     // CWeapon::Fire passes [ped+0x64] to PlayOneShot
constexpr uint32_t ped_currentSlot = 0x498;
constexpr uint32_t ped_selectedSlot = 0x554;
constexpr uint32_t entity_pos = 0x34;          // FindPlayerCoors reads ped+0x34 (3 floats)
constexpr uint32_t entity_modelIndex = 0x5C;   // CPickup::Update reads [object+0x5C]
constexpr uint32_t kMaxAmmo = 99999;

// ---- Functions --------------------------------------------------------------------------------------------------
constexpr uint32_t FindPlayerPed = 0x4A1150;                   // cdecl () -> ped
constexpr uint32_t FindPlayerCoors = 0x4A1030;                 // cdecl (CVector* out) -> out
constexpr uint32_t CPed_GiveWeapon = 0x4CF9B0;                 // thiscall (type, ammo) ret 8
constexpr uint32_t CPlayerPed_MakeChangesForNewWeapon = 0x4F2560; // thiscall (char) ret 4
constexpr uint32_t CWeaponInfo_Initialise = 0x564EA0;          // cdecl ()
constexpr uint32_t CWeapon_UpdateWeapons = 0x55C310;           // cdecl ()
constexpr uint32_t CWeapon_Fire = 0x55C380;                    // thiscall (shooter, fireSource) ret 8 -> bool
constexpr uint32_t CPickups_WeaponForModel = 0x4306F0;         // cdecl (model) -> weaponType
constexpr uint32_t CPickups_GenerateNewOne = 0x4304B0;         // cdecl (x,y,z, model, type, qty) -> handle
constexpr uint32_t cDMAudio_PlayOneShot = 0x57C840;            // thiscall (entity, u16 sound, float vol) ret 0xC
constexpr uint32_t CPickup_Update_begin = 0x430860, CPickup_Update_end = 0x431510;
constexpr uint32_t CPickups_aPickUps = 0x878C98;
constexpr uint32_t kPickupSize = 0x1C, kNumGeneralPickups = 0x140;
namespace pickup { constexpr uint32_t type = 0, removed = 1, quantity = 2, object = 4, timer = 8, model = 0xC, pos = 0x10; }

// Sound events (CWeapon::Fire / CWeapon::Update emit them; numbering confirmed by re3 soundlist.h order)
constexpr uint16_t SOUND_WEAPON_SHOT_FIRED = 0x2F;
constexpr uint16_t SOUND_WEAPON_RELOAD = 0x30;

// ---- Models / TXD / streaming --------------------------------------------------------------------------------------
constexpr uint32_t ms_modelInfoPtrs = 0x83D408;
constexpr int kModelInfoSize = 5500;
constexpr uint32_t simpleModelStoreCount = 0x885BB4;
constexpr int kSimpleModelStoreSize = 5000;
constexpr uint32_t CModelInfo_GetModelInfoByName = 0x50B860;   // cdecl (name, int* id) -> CBaseModelInfo*
constexpr uint32_t CFileLoader_LoadObject = 0x477040;          // cdecl (const char* ideLine)
constexpr uint32_t CFileLoader_LoadAtomicFile_stream = 0x476930; // cdecl (RwStream*, modelId) -> bool
constexpr uint32_t RwStreamOpen = 0x5A3FE0, RwStreamClose = 0x5A3F10;
constexpr uint32_t CTxdStore_FindTxdSlot = 0x5275D0, CTxdStore_AddTxdSlot = 0x5274E0, CTxdStore_LoadTxd = 0x5276B0,
                   CTxdStore_SetCurrentTxd = 0x5278C0, CTxdStore_PushCurrentTxd = 0x527900, CTxdStore_PopCurrentTxd = 0x527910;
constexpr uint32_t CStreaming_ms_aInfoForModel = 0x6C7088;     // struct base; 0x14 each: loadState @+8, flags @+9
constexpr uint32_t kStreamInfoSize = 0x14;
constexpr int kStreamTxdOffset = 0x157C;                        // txd stream id = 5500 + slot
constexpr uint8_t STREAMSTATE_LOADED = 1, STREAMFLAGS_DONT_REMOVE = 1;

// ---- RenderWare (cdecl) -----------------------------------------------------------------------------------------
constexpr uint32_t RwImageCreate = 0x5A9120;            // (w,h,depth)
constexpr uint32_t RwImageDestroy = 0x5A9180;
constexpr uint32_t RwImageAllocatePixels = 0x5A91E0;
constexpr uint32_t RwImageFindRasterFormat = 0x5BBF80;  // (img, type, *w,*h,*depth,*flags)
constexpr uint32_t RwRasterCreate = 0x5AD930;           // (w,h,depth,flags)
constexpr uint32_t RwRasterSetFromImage = 0x5BBF50;     // (raster, img)
constexpr uint32_t RwTextureCreate = 0x5A72D0;          // (raster)
constexpr uint32_t RwTextureSetName = 0x5A73B0;         // (tex, name)
constexpr uint32_t CHud_Sprites = 0x95CB9C;             // CSprite2d[] (4 bytes each = RwTexture*), indexed by weapon type
namespace rwimg { constexpr uint32_t stride = 0x10, pixels = 0x14; }

// ---- Animations -------------------------------------------------------------------------------------------------
constexpr uint32_t CAnimManager_GetAnimAssociationByName = 0x403600; // cdecl (group, name) -> assoc* (animId @ +0x2C)
constexpr uint32_t CAnimManager_LoadAnimFile = 0x403A10;             // cdecl (path)
constexpr uint32_t CAnimManager_GetAnimationBlock = 0x4034A0;        // cdecl (name) -> CAnimBlock*
constexpr uint32_t CAnimManager_GetAnimationByBlock = 0x4034F0;      // cdecl (name, block) -> hierarchy*
constexpr uint32_t CAnimManager_ms_aAnimAssocGroups = 0x8F583C;      // pointer to array of {assocList*, numAssociations}
constexpr uint32_t ms_numAnimBlocks = 0x885AF8, ms_numAnimations = 0x8E2DD4;
constexpr int kNumAnimBlocks = 2, kNumAnimations = 250;              // fixed static capacity
constexpr uint32_t kAnimAssocSize = 0x40;                            // GetAnimation(id) = (id<<6)+assocList
constexpr uint32_t CAnimBlendAssociation_ctor = 0x401460, CAnimBlendAssociation_Init_clump = 0x401560;
constexpr uint32_t RpAnimBlendClumpInit = 0x405480, RpClumpDestroy = 0x59F500, operator_new_array = 0x59CC90;
constexpr uint32_t AnimAssocDefinitions = 0x5EB724;                  // def[0] = "man"/"ped", model 1, 173 anims
namespace assoc { constexpr uint32_t hierarchy = 0x14, animId = 0x2C, flags = 0x30; }
constexpr int kAssocClumpModel = 1;                                  // def[0].modelIndex

// ---- Save / load ------------------------------------------------------------------------------------------------
constexpr uint32_t site_SaveSlot_call = 0x485202;      // CMenuManager::Process: call C_PcSave::SaveSlot
constexpr uint32_t C_PcSave_SaveSlot = 0x591EC0;       // thiscall (slot) ret 4 -> bool
constexpr uint32_t site_GenericLoad_call = 0x48C7CC;   // CGame::InitialiseWhenRestarting, after ReInit
constexpr uint32_t GenericLoad = 0x590A00;             // cdecl () -> bool
constexpr uint32_t saveFileName = 0x8E2CBC;            // buffer SaveSlot fills and opens
constexpr uint32_t loadFileName = 0x9403C4;            // buffer filled by the load-slot path, opened by GenericLoad

// ---- Init / frame call sites --------------------------------------------------------------------------------------
constexpr uint32_t site_InitialiseWeapons_callInitialise = 0x55C2D0;  // call 0x564EA0
constexpr uint32_t site_GameProcess_callUpdateWeapons = 0x48C940;     // call 0x55C310
constexpr uint32_t CTimer_ms = 0x885B48;                              // CTimer::m_snTimeInMilliseconds
constexpr uint32_t Prefs_SfxVolume = 0x5F2E48;                        // int 0..127

// ---- Prologues we detour (7 bytes each: clean instruction boundary, position independent) ---------------------------
constexpr uint8_t sig_GiveWeapon[7]     = {0x53, 0x56, 0x55, 0x8B, 0x6C, 0x24, 0x10};
constexpr uint8_t sig_WeaponForModel[7] = {0x0F, 0xB7, 0x05, 0x14, 0x5B, 0x5F, 0x00};
constexpr uint8_t sig_PlayOneShot[7]    = {0x83, 0xEC, 0x08, 0x8B, 0x54, 0x24, 0x10};
constexpr uint8_t sig_Fire[7]           = {0x53, 0x56, 0x57, 0x55, 0x83, 0xEC, 0x28};
constexpr uint8_t sig_GetWeaponInfo[8]  = {0x8B, 0x44, 0x24, 0x04, 0x6B, 0xC0, 0x54, 0x05};
} // namespace gta3
