# HANDOFF: GTA III 1.0 Weapon Loader (give this file + weaponloader.zip to Claude in a new chat)

Say: "Continue the GTA III weapon loader from HANDOFF.md. Read it first."

## Goal (from the user)
An extensible weapon loader for **GTA III PC 1.0** (ASI plugin), inspired by Maxo's Vehicle Loader:
- Weapons defined by external `weapon.xml` + DFF / TXD / PNG icon / **OGG-only** audio (NO WAV support, no WAV fallback, ever).
- **Core rule:** a slot holds ONE weapon at a time. Category/slot shared by many definitions. Picking up Desert Eagle replaces the
  original pistol; Colt Python then replaces the Deagle; the original pistol replaces either. Must work through real pickups,
  survive save/load, keep original weapons/missions/saves compatible, keep the exe version guard and safe patching.
- Do not invent addresses, do not claim untested code works, distinguish in-game testing from code-level verification.

## Inputs the user supplied
gta3.exe (1.0, PE timestamp 0x3CCECE14), gta3_1_0_kenking_.idb (IDA1 v6, matches the exe except 12 unrelated bytes),
weapon.dat, default.dat, default.ide, gta3.dat, main.scm, american.gxt, and the **re3 source** (git bundle, 2021-09-06).
Not supplied / not needed: gta3_11en_m.idb (v1.1; user said ignore). The user says the real source/IDA files exist but the
sandbox only ever had these files.

## Environment limits found (they may differ in a new chat; re-check first)
No Windows compiler, no mingw, no Wine, no 32-bit headers, no internet. So: **no .asi was ever built and nothing ran in-game.**
Available: g++ (host), Python, Pillow, numpy, ffmpeg+libvorbis (used only to make test references), objdump (disassembles the PE).
I wrote my own IDB parser (ID0 B-tree v2 with indent-compressed leaves, ID1 byte flags, NAM) to read symbols; it was used to
name functions. Helper scripts lived in /home/claude/re (lost between chats; re-derivable).

## Key design decision
GTA III has no inventory list: `CPed::m_aWeapons[13]` is indexed by weapon type and everything tests `m_aWeapons[i].type == i`.
A "slot" IS one of the 13 original types (0 Unarmed,1 Bat,2 Colt45,3 Uzi,4 Shotgun,5 AK47,6 M16,7 Sniper,8 Rocket,9 Flame,
10 Molotov,11 Grenade,12 Detonator; weapon.dat row order differs, rows are matched by name). Custom weapons are **variants**
living in a slot (fist and detonator excluded), borrowing that slot's firing logic. Cycling is untouched (it already walks owned slots).
Limitation: `CWeaponInfo` is per type, so NPCs with that base weapon share the active variant's stats.

## Project layout (weaponloader/)
- `core/` portable, host-tested: `xml_mini.hpp`, `weapon_core.hpp` (slots, WeaponDef, parser/validator, Registry, SlotState, loadPack),
  `variant_logic.hpp` (variantForPickup, collectSaveMap, planLoad, slotsToRevert), `save_state.hpp` (sidecar GTA3sfN.b.wl bound to save by size+FNV hash),
  `model_ids.hpp` (hash-derived stable ids in 5100..5499), `png_decode.hpp` (own inflate), `ogg_vorbis.hpp` (own Vorbis decoder),
  `mixer_core.hpp`, `image_util.hpp`, `ifp_probe.hpp`.
- `plugin/` Windows only, UNTESTED: `gta3_addrs.hpp` (verified address table), `engine.hpp` (wrappers, patchCall, detour7, RW texture),
  `audio_win.hpp` (waveOut mixer), `runtime.hpp` (all logic), `plugin.cpp` (hooks + DllMain).
- `tests/` (`run_all.sh`: test_core 135 checks, PNG vs Pillow + fuzz, OGG vs ffmpeg + fuzz), `tools/verify_exe.py`,
  `pack/weaponloader/` (loader.ini + 4 example weapon.xml: desert_eagle, colt_python (pistol slot), m4, ruger_556 (ak47 slot); no binary assets),
  `docs/` (REVERSE_ENGINEERING_REPORT.md incl. Appendix A, TESTING_REPORT.md), README.md, INSTALL_GUIDE.md, build_asi.bat, .github/workflows/build.yml (cloud build).

## Verified engine facts (all in gta3_addrs.hpp / RE report; `tools/verify_exe.py` rechecks them)
aWeaponInfo 0x6503EC stride 0x54 (+4 range,+8 rate,+0xC reload,+0x10 clip,+0x14 damage,+0x34/+0x38 anim ids,+0x3C..0x48 loop/fire frames,+0x4C model,+0x50 flags);
CPed weapons +0x35C stride 0x18 (type,state,clip,total), current slot +0x498, selected +0x554, audio entity +0x64, pos +0x34;
hook sites: call 0x55C2D0 (CWeaponInfo::Initialise), 0x48C940 (UpdateWeapons per frame), 0x485202 (SaveSlot), 0x48C7CC (GenericLoad);
7-byte detours: GiveWeapon 0x4CF9B0, WeaponForModel 0x4306F0, cDMAudio::PlayOneShot 0x57C840, CWeapon::Fire 0x55C380;
sounds 0x2F shot / 0x30 reload; pickups: CPickup::Update 0x430860 dispatches by type and calls GiveWeapon(WeaponForModel(model)), GenerateNewOne 0x4304B0, aPickUps 0x878C98 (0x1C each);
models: LoadObject(line) 0x477040 ("%d %s %s %d %f %d"), LoadAtomicFile(stream,id) 0x476930 (returns success even without clump: check atomic at modelinfo+0x30),
ms_modelInfoPtrs 0x83D408 (5500), TXD LoadTxd 0x5276B0 loops forever on missing file (check existence), stream info base 0x6C7088 (0x14 each, state@+8, flags@+9, txd stream id 5500+slot);
HUD icon = CHud::Sprites[type] @0x95CB9C; save name buffers 0x8E2CBC (save) / 0x9403C4 (load); std anim group pointer 0x8F583C (element 0x40 bytes).

## State of each feature
All six requested features are IMPLEMENTED IN CODE; only portable logic is host-tested. Plugin: syntax-checked against stub headers only.
Highest-risk untested pieces: custom-IFP append to the std anim group (array cookie assumption), RenderWare texture creation on this renderer,
empty-click (needs Fire to be called on empty weapon), saves containing custom pickups after uninstalling a weapon (crash).

## What the user has to do / next steps
1. Build the .asi (no compiler was available to me): `build_asi.bat` (VS Build Tools x86 or MinGW i686) or push the folder to GitHub and run the "build" workflow.
2. Supply DFF/TXD/PNG/OGG assets; run `python tools/verify_exe.py gta3.exe`; follow INSTALL_GUIDE.md section 5; return `weaponloader.log` and any crash/compile errors.
3. Then: fix compile errors, diagnose from the log, iterate per feature (pickups -> models -> icon -> audio -> persistence -> animations).
Be honest in reports: separate "host-tested" from "tested in game".
