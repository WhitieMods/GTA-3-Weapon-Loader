# GTA III 1.0 Extensible Weapon Loader

An ASI plugin for **GTA III PC 1.0** that adds custom weapons from external folders (`weapon.xml` + DFF/TXD/PNG/OGG).
Custom weapons are *variants* that live in one of the game's real weapon slots: collecting one **replaces** the weapon
currently in that slot (original pistol -> Desert Eagle -> Colt Python ...), never accumulates.

> **Honest status.** Everything is written against addresses verified in your `gta3.exe` and cross-checked with the re3
> source, and all logic that can run on a Linux host is tested (135 unit checks, PNG and OGG decoders verified against
> Pillow / libvorbis, fuzzed). **The Windows plugin itself has never been compiled to a DLL or run in GTA III**; no
> Windows toolchain or game was available. Read `docs/TESTING_REPORT.md` before trusting any feature.

## Feature status
| # | Feature | Implementation | Verified |
|---|---|---|---|
| 1 | Custom pickup models + world pickups | `registerModel`, `WeaponForModel`/`GiveWeapon` hooks, `<spawns>` | logic host-tested; **engine side untested** |
| 2 | DFF/TXD loading | engine's own IDE path (`CFileLoader::LoadObject`) + `LoadAtomicFile` with the weapon's TXD current | **untested in game** |
| 3 | PNG HUD icons | own PNG decoder -> RenderWare image/raster/texture -> `CHud::Sprites[slot]` | decoder host-tested vs Pillow; **engine side untested** |
| 4 | OGG-only audio | own Vorbis decoder; `cDMAudio::PlayOneShot` events 0x2F (shot) / 0x30 (reload) swapped for the player's weapon; `CWeapon::Fire` hook for an empty-click; waveOut mixer | decoder host-tested vs libvorbis; **hooks/output untested** |
| 5 | Animations | choose any std ped animation by name; optional custom IFP appended to the std group; loop/fire-frame overrides | **untested** (custom IFP = highest risk) |
| 6 | Save/load persistence | sidecar `GTA3sfN.b.wl` bound to the save by size+hash; written after `C_PcSave::SaveSlot`, restored before `GenericLoad` | logic host-tested on real files; **hooks untested** |
| - | Same-slot replacement via real pickups | `variantForPickup` (host-tested) driven by the hooks | logic host-tested; **engine untested** |

There is no WAV code anywhere: the only audio path is OGG -> own decoder -> raw PCM -> waveOut.

## How it works (short)
GTA III has no inventory list: `CPed::m_aWeapons[13]` is indexed by weapon type and the engine requires
`m_aWeapons[i].type == i`. A "slot" is therefore one of the 13 original types and a custom weapon borrows its slot's firing
logic. On collecting a custom pickup the loader (a) lets the engine do its normal `GiveWeapon` ammo logic, (b) records the
variant, (c) writes the variant's stats / model id / animation ids into that slot's `CWeaponInfo`, (d) swaps the HUD icon
sprite, and (e) refreshes the held weapon via the engine's own `MakeChangesForNewWeapon`. Cycling is untouched.
Details and addresses: `docs/REVERSE_ENGINEERING_REPORT.md`. Verify your exe: `python tools/verify_exe.py gta3.exe`.

## Build / install
See **`INSTALL_GUIDE.md`**. Host tests: `tests/run_all.sh`.

## weapon.xml
```xml
<weapon id="desert_eagle">                    <!-- a-z 0-9 _ ; <=48; "original_" reserved; unique -->
  <name>Desert Eagle</name>
  <slot>pistol</slot>                         <!-- bat pistol uzi shotgun ak47 m16 sniper rocket flamethrower molotov grenade -->
  <category>pistol</category>                 <!-- optional; must match the slot's category -->
  <stats damage="55" range="32" fireRate="450" reload="600" clipSize="7"/>   <!-- all optional -->
  <ammo pickupAmmo="21" maxAmmo="250"/>
  <assets>                                    <!-- relative paths, no ".." ; missing file = feature skipped with a warning -->
    <model>m.dff</model>                      <!-- held weapon (and pickup if no <pickupModel>) -->
    <pickupModel>p.dff</pickupModel>
    <txd>t.txd</txd>                          <!-- shared by both models -->
    <icon>icon.png</icon>                     <!-- non-interlaced PNG, <=4096x4096 -->
  </assets>
  <audio volume="1.0"> <fire>fire.ogg</fire> <reload>reload.ogg</reload> <empty>empty.ogg</empty> </audio>   <!-- .ogg ONLY -->
  <animations anim1="WEAPON_hgun_body" anim2="" ifp="my.ifp" block="MyBlock"
              loopStart="" loopEnd="" frameFire="0.09" frame2Fire=""/>
  <spawns> <spawn x="814" y="-940" z="15" type="2" ammo="21"/> </spawns>   <!-- type 2 respawning, 3 once, 14 slow respawn -->
</weapon>
```
Model requirements: DFF with **one clump containing one atomic** (it is attached to a simple model by id; frame name needs no
particular value). Textures must be in the weapon's `<txd>`. Icon PNG fills the whole texture; the HUD draws UVs 0.015..1.0
so ~1.5% of the top/left edge is cropped. Animation names are those used in `weapon.dat` (e.g. `WEAPON_hgun_body`,
`WEAPON_AK_body`, `WEAPON_pump`). A custom IFP must hold exactly one block, fit the engine's fixed tables (2 blocks / 250
animations total) and use the ped skeleton; the loader checks the header and refuses files that do not fit.

## Known limitations
1. Not run in-game (see above). Expect first-compile fixes.
2. `CWeaponInfo` is per weapon *type* and shared by all peds: while a variant is active, NPCs with that base weapon use its stats/animations too.
3. World pickups of custom weapons store custom model ids in the game's save. IDs are derived from the weapon id (stable), but if you uninstall a weapon whose pickup exists in a save, loading that save will crash. Collect or avoid them first.
4. Custom sounds replace only the **player's** shot (0x2F) and reload (0x30) events. NPC gunfire, the flamethrower (0x34) and melee sounds are untouched. Output is stereo (not positional); volume follows the in-game SFX slider.
5. The empty click relies on the engine calling `CWeapon::Fire` on an empty weapon, which was not verified.
6. `<spawn>` pickups are created on new game/restart only, never after loading a save (saved pickups already contain them).
7. Scripts that call GIVE_WEAPON do not change the active variant (they only add ammo to the slot); picking up a vanilla pickup of that slot restores the original.
8. Spawn example coordinates are samples (x/y near the player start in `main.scm`; z is a guess): use the F12 debug key for testing.
9. `std::filesystem` needs g++ >= 9 (MinGW) or MSVC 2017+.
