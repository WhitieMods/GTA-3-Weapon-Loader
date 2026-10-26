# Reverse-engineering report: GTA III PC 1.0 weapon system

Everything marked **[V]** was read directly from the supplied `gta3.exe` (objdump disassembly / raw bytes) and named
using the supplied `gta3_1_0_kenking_.idb`. Everything marked **[U]** is *unverified* and is not relied on by any code.
No address in the source is recalled from memory.

## 1. Input verification

| Item | Result |
|---|---|
| Files received | `gta3.exe`, `gta3_1_0_kenking_.idb`, `weapon.dat`, `default.dat`, `default.ide`, `gta3.dat`, `main.scm` |
| **Not received** | `american.gtx`, `gta3_11en_m.idb` (the second IDB). The file you named `weapons.dat` arrived as `weapon.dat`. |
| `gta3.exe` | PE32, 2,383,872 bytes, TimeDateStamp `0x3CCECE14`, image base `0x400000`, SHA-256 `b8df59d7...bfba744` |
| IDB format | `IDA1` container, version 6 (32-bit). I parsed its ID0 B-tree (710,976 records, matches the header count), ID1 and NAM sections with my own reader (no IDA available here). 48,011 symbol names recovered. |
| IDB <-> EXE match | **[V]** IDB holds 2,224,128 bytes of image data that overlap the EXE; 2,224,116 are identical. Only 12 bytes differ, none in weapon code: `0x566A15`, `0x566A58` (cSampleManager::Initialise), `0x5811E9-ED` (psInitialize), `0x581C44-45`, `0x581C52-53` (WndProc), `0x604B24` (a string). So the IDB describes this executable; the 12 bytes look like small patches on one side. |
| `gta3_11en_m.idb` | Cannot be compared, not supplied. Not needed for 1.0. |

## 2. The central finding: there is no separate "slot" in GTA III

**[V]** `CPed::GiveWeapon` (`0x4CF9B0`) addresses `ped + type*0x18 + 0x35C`. `CPed::GetWeaponSlot` (`0x4CFA40`) returns
`type` if `m_aWeapons[type].m_eType == type`, else `-1`. `CPlayerPed::ProcessWeaponSwitch` (`0x4F2310`) cycles `i = 0..12`
(`cmp dl,0xD`) and accepts slot `i` only when `m_aWeapons[i].m_eType == i`. `CHud::Draw` indexes the weapon icon by the
current slot's type (`CHud::Sprites` at `0x95CB9C`, 4 bytes/entry). ModelForWeapon/WeaponForModel are fixed jump tables.

Consequences:

* A "slot" **is** one of the 13 `eWeaponType` values. The engine has no inventory list, so "one pistol per slot" is
  exactly how the game already behaves for its own weapons.
* You cannot add a 14th weapon type without changing array sizes embedded in `CPed` (0x35C + 13*0x18 = 0x494, fields follow),
  the cycle loops, HUD sprite table and save format. Rejected as unsafe.
* **Strategy used:** a custom weapon is a **variant** that occupies one of the original slots. Its firing logic is the
  slot's original weapon (the "archetype"); its stats/assets are its own. Same-slot replacement is then the natural
  behaviour: the slot holds exactly one definition at a time. This is why `<slot>` is mandatory and why a pistol-class
  weapon can never be given a shotgun's firing code.
* `fist` (0) and `detonator` (12) are not offered to variants (mission/script-critical).
* Note for your rifle example: the game has **two** rifle slots (`ak47`=5, `m16`=6). Both have category `assault_rifle`.
  Choose either per weapon; weapons in different slots are independently owned.

### eWeaponType numbering **[V]** (from `ms_aWeaponNames` @0x603334, decoded from the exe)
0 Unarmed, 1 BaseballBat, 2 Colt45, 3 Uzi, 4 Shotgun, 5 AK47, 6 M16, 7 SniperRifle, 8 RocketLauncher, 9 FlameThrower, 10 Molotov, 11 Grenade, 12 Detonator.
**Warning:** `weapon.dat` row order is different (Grenade/Molotov/Rocket/Flame come before AK47...). Rows are matched by name, so file order is irrelevant, but never assume row index == type.

## 3. Verified structures and addresses

| Thing | Value | Verified how |
|---|---|---|
| `CWeaponInfo::aWeaponInfo` | `0x6503EC`, 0x54 bytes each, 13 entries | `GetWeaponInfo` @0x564FD0 is `imul eax,[esp+4],0x54; add eax,0x6503EC` |
| CWeaponInfo +0x04/+0x08/+0x0C/+0x10/+0x14 | range / fire rate / reload / ammo-in-clip / damage | stores in `LoadWeaponData` @0x564FE0 follow weapon.dat columns C..G; +0x10 compared with ammo in `MakeChangesForNewWeapon` and used by the HUD |
| CWeaponInfo +0x4C / +0x50 | model id / flags | `AddWeaponModel` path reads +0x4C; `MakeChanges` tests +0x50 & 0x40 (CANAIM) |
| CWeaponInfo +0x18..+0x30, +0x34/+0x38, +0x3C..+0x48 | speed, radius, lifespan, spread, fire offset / anim pointers / anim timings | **[U]** per-field mapping inferred from column order; not used by the loader |
| `CPed::m_aWeapons` | `+0x35C`, 13 x `CWeapon`(0x18) | GiveWeapon/ClearWeapons/ProcessWeaponSwitch |
| `CWeapon` | +0 type, +4 state, +8 ammoInClip, +0xC totalAmmo | GiveWeapon/MakeChanges; agrees with IDB struct member names |
| `CPed` +0x498 / +0x499 / `CPlayerPed` +0x554 | current slot (byte) / owned count (byte) / selected slot (byte) | GiveWeapon, SetCurrentWeapon, ProcessWeaponSwitch, CPickup::Update |
| Ammo cap | 99999 | `cmp eax,0x1869F` in GiveWeapon |
| `CPed::GiveWeapon(type, ammo)` | `0x4CF9B0` thiscall, `ret 8` | If slot owned: ammo += n (cap), then `CWeapon::Reload`. Else `CWeapon::Initialise`, `[+0x499]++`. |
| `CPlayerPed::MakeChangesForNewWeapon(char)` | `0x4F2560`, `ret 4` | Calls SetCurrentWeapon, sets clip = min(ammo, info+0x10) |
| `FindPlayerPed` | `0x4A1150` | IDB name + usage |
| Pickup collection | `CPickup::Update` `0x430860` | Weapon branch: `WeaponForModel(model@[obj+0x5C])` -> `GiveWeapon(type, ammo)` -> `GetWeaponSlot` -> `[ped+0x554]=slot` -> front-end sound `0x47+model-170` |
| `CPickups::ModelForWeapon` / `WeaponForModel` | `0x430690` / `0x4306F0` | jump tables decoded: model(170..181) <-> type is fixed (e.g. 173<->2 pistol) |
| Per-type tables | words @`0x5ED8D4` (default pickup ammo? 45 pistol, 125 uzi...), `0x5ED8FC`, `0x5ED924` (price-like: 250, 800, 1500...) | **[U]** purpose inferred from use; not used by the loader |
| HUD weapon icon | `CHud::Sprites[type]` (`0x95CB9C`) | `ebx=m_aWeapons[cur].type; ebx*4+0x95CB9C` in `CHud::Draw` @0x506224 |
| Cycle input | `CPad::CycleWeaponLeftJustDown` `0x493910`, `...RightJustDown` `0x493940` | called from `ProcessWeaponSwitch`. **[U]** that keyboard and pad bindings both feed these CPad methods is inferred from the structure, not traced. |
| Init hook | `CWeapon::InitialiseWeapons` `0x55C2D0` starts with `call 0x564EA0` (CWeaponInfo::Initialise) | callers `0x48C1E7` (CGame::Initialise), `0x48C535` (ReInit): runs on boot/new game/restart/load |
| Frame hook | `call 0x55C310` (CWeapon::UpdateWeapons) at `0x48C940` inside `CGame::Process` | disassembly |

## 4. Consequences for each requirement

* **Cycling (req. 9):** unchanged. **[V]** the game already cycles owned slots (`m_aWeapons[i].type == i`); a replaced slot is simply a different definition
  in the same slot, so no input code needs touching and the loader hardcodes no key. Controller behaviour is expected to be identical (same function) but is **[U]** until tested.
* **Pickup replacement (req. 3):** `GiveWeapon` already means "slot owned => add ammo". The loader's replacement keeps that
  ammo semantic (one ammo pool per slot, as for original weapons) and swaps the definition.
* **Stats:** `CWeaponInfo` is per weapon *type* and `GetWeaponInfo` receives no ped. Variant stats are therefore global
  while active: NPCs carrying that base weapon also use them. Per-owner stats would need hooks in every caller. **Limitation.**
* **HUD icon:** exactly one `CSprite2d` per type. Swapping the `RwTexture*` in `CHud::Sprites[slot]` on replacement is the
  clean approach. **[U]** texture creation from PNG (RW API addresses and the D3D/OpenGL backend) not yet verified. Note the
  IDB contains `RwOpenGLVertex` names, so the renderer layer needs checking before PNG upload.
* **Model/TXD/pickup models:** `CModelInfo::ms_modelInfoPtrs` is at `0x83D408`. Weapon model ids 170..181 are fixed in the
  tables above, `default.ide` reserves nearby ids. Registering new model ids (CModelInfo array size, IDE-style registration
  functions, streaming) is **[U]**; this is why custom *pickup* models are not implemented yet.
* **Audio:** `cDMAudio::PlayFrontEndSound` `0x57CC20`, `DMAudio` object `0x95CDBE`, `cSampleManager::Initialise` `0x566530` exist.
  Weapon fire sounds are chosen inside the audio manager by weapon type (**[U]**, not yet read). OGG decoding + routing
  into the sample manager is not verified.
* **Animations:** `CWeaponInfo` +0x34/+0x38 hold resolved animation references and `weapon.dat` names them (e.g. `WEAPON_hgun_body`).
  Names must exist in already-loaded IFP data; new animations require IFP loading (**[U]**). XML `<animations>` is parsed but unused.
* **Save/load:** **[U]** I have not read the save code. Symbols exist (`CPlayerInfo::SavePlayerInfo` `0x4A0960`, `LoadPlayerInfo` `0x4A0B20`, `CPickups::Save/Load`) but I did not disassemble them, so I do not know how weapons are serialised. The active variant is **not** persisted: it reverts to the original after load/restart (a sidecar file keyed by save slot is the plan).
* **Missions / SCM:** `main.scm` opcodes (`GIVE_WEAPON_TO_PLAYER`, `HAS_PLAYER_GOT_WEAPON`, `GET_CURRENT_PLAYER_WEAPON`...) use
  the original type numbers; because variants keep the base type, scripts should see the base weapon. This is a design expectation: `main.scm` was **not** audited for weapon-specific logic (e.g. missions that check or strip particular weapons), and nothing was run.


---
# Appendix A: verified in the second pass (with the re3 source available)

All items were checked in `gta3.exe`; re3 (supplied git bundle, master at 2021-09-06) was used to confirm structure layouts and
semantics. `tools/verify_exe.py` re-checks the patched sites and tables mechanically.

| Topic | Verified fact |
|---|---|
| Layout agreement | `CWeaponInfo` offsets (+0x04 range ... +0x34/+0x38 anim ids, +0x3C..+0x48 loop/fire frames, +0x4C model, +0x50 flags), `CStreamingInfo` (0x14 bytes, loadState @+8, flags @+9, `STREAMFLAGS_DONT_REMOVE=1`), `CPickup` (0x1C bytes, type@0, qty@2, object@4, model@0xC, pos@0x10), `CAnimBlendAssociation::animId @+0x2C` (vptr included) all match re3 |
| Model registration | `CFileLoader::LoadObject(line)` @0x477040 parses `"%d %s %s %d %f %d"` (id, name, txd, 1 LOD, distance, flags) and calls `AddSimpleModel`, `SetTexDictionary`, etc. Feeding it a synthetic IDE line reproduces the engine's normal object registration. `ms_modelInfoPtrs`=0x83D408 (5500), simple-model store limit 5000 (count @0x885BB4) |
| DFF attach | `CFileLoader::LoadAtomicFile(RwStream*, id)` @0x476930 sets the target model and runs `SetRelatedModelInfoCB`; it returns success even when no clump chunk exists, so the loader checks `m_atomics[0]` (+0x30, from `SetAtomic` @0x517950) |
| TXD | `CTxdStore::LoadTxd(slot,path)` @0x5276B0 loops forever if the file cannot be opened (retry at 0x5276C1): existence is checked first. Stream id of a TXD slot is 5500+slot |
| Streaming | `RequestModel` keeps a LOADED model with `flags&3` out of the removable list, so custom models marked loaded+DONT_REMOVE are never unloaded |
| Pickups | `CPickup::Update` @0x430860 dispatches by pickup type, reads the entity model `[obj+0x5C]`, and for weapons does `GiveWeapon(WeaponForModel(model), qty != 0 ? qty : default)`; `GenerateNewOne(x,y,z,model,type,qty)` @0x4304B0 creates the `CObject` directly from the model index. The pickup sound's second argument is only compared to 1.0 (frequency choice), so large custom model ids are harmless |
| HUD | `CHud::Draw` draws `Sprites[currentSlotType].Draw(rect, colour, 0.015.., ...)` (`ebx*4+0x95CB9C`) |
| Audio | `cDMAudio::PlayOneShot(entity,u16 sound,float vol)` @0x57C840 (this = DMAudio 0x95CDBE). `CWeapon::Fire` emits 0x2F after a successful shot by a ped shooter, `CWeapon::Update` emits 0x30 shortly before a reload ends; ids match re3's sound list (0x2E bat, 0x2F shot, 0x30 reload, 0x34 flamethrower, 0x37/0x38 hits). `Fire` @0x55C380 returns false silently on an empty weapon (no stock click) |
| Save | `C_PcSave::SaveSlot(slot)` @0x591EC0 builds the file name in 0x8E2CBC, opens it, calls `GenericSave`; called from the menu at 0x485202. The load path puts the name in 0x9403C4; `GenericLoad` @0x590A00 is called at 0x48C7CC in `CGame::InitialiseWhenRestarting`, *after* `ReInitGameObjectVariables` (which re-runs `InitialiseWeapons`) |
| Init order | At the first `InitialiseWeapons`, IDEs, streaming, TXD store and the animation groups are already built (re3 `CGame::Initialise` order), so free model ids can be allocated against real occupancy |
| Animations | Weapon animation ids index `ASSOCGRP_STD` (group "man", block "ped", 173 entries, clump model 1); `LoadWeaponData` resolves names with `GetAnimAssociation(0,name)` and stores `assoc->animId` into +0x34/+0x38 (anim2 only if the name is not "null"). Group element = 0x40 bytes (`GetAnimation(id)` = `(id<<6)+list`). Engine tables are fixed: 2 anim blocks, 250 hierarchies |
| Not verified | The MSVC array-cookie convention assumed when growing the std animation group; behaviour of `RwImageFindRasterFormat` on this renderer; that `CWeapon::Fire` is called for an empty weapon; mission scripts' weapon logic |
