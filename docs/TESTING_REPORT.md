# Testing report

**Nothing has been run inside GTA III.** This environment has no game, no Windows, no Windows compiler and no Wine.
"Tested" below therefore means *tested on a Linux host with real reference data*, and says nothing about in-game behaviour.

## Legend
* **Host-tested**: the code runs here and is checked against an independent reference or against explicit expectations.
* **Syntax-checked**: compiled with `g++ -fsyntax-only` against hand-written stub Win32 headers. This catches typos and type errors; it does **not** produce a DLL and proves nothing about behaviour.
* **Verified against exe**: an address, call site, prologue or table was compared to the supplied `gta3.exe` (`tools/verify_exe.py`).
* **Untested**: written, never executed.

## What was actually executed (all reproducible with `tests/run_all.sh`)
| Component | Result | Evidence |
|---|---|---|
| PNG decoder (own inflate) | **Host-tested.** Bit-identical to Pillow on RGBA/RGB/gray/gray+alpha/palette/1-bit with stored, fixed and dynamic deflate blocks; 400 fuzzed (truncated/corrupted) files, 0 crashes under ASan+UBSan | `tests/run_png_tests.py` |
| OGG Vorbis decoder | **Host-tested.** Matches ffmpeg/libvorbis within 1e-6 (float rounding) on mono/stereo/5.1, 22.05/44.1/48 kHz, encoder quality -1..10 with transients (short blocks). Fuzzing found a real bug (a coupled channel with an unused floor read an empty vector); fixed; then 600 fuzzed files, 0 crashes | `tests/run_ogg_tests.py` |
| Weapon XML parser + validation (incl. OGG-only rule, spawns, animations, path safety) | **Host-tested** | `tests/test_core.cpp` |
| Same-slot replacement, pickup decision logic, save sidecar, load plan/revert (the exact spec scenario: original -> Deagle -> save -> reload -> Python -> original) | **Host-tested at the decision-logic level** using the same functions the hooks call. A mutation check confirmed these tests fail when the logic is broken | `test_scenarios`, `core/variant_logic.hpp` |
| Save sidecar (size+hash binding, stale/removed/missing cases) | **Host-tested** against real files | `test_save_sidecar` |
| Model-id allocation (deterministic, probing, full range) | **Host-tested** | `test_model_ids` |
| Audio mixer math (resampling, panning, voice expiry, clipping) | **Host-tested** | `test_mixer` |
| IFP header probe (block name/count, capacity guard inputs) | **Host-tested** incl. truncation | `test_ifp_probe` |
| 135 checks in `test_core`, 0 failed | | |
| Address table / hook sites | **Verified against exe** (all 4 call sites, 5 prologues, weapon-name table, std anim group) | `tools/verify_exe.py` (also rejects a tampered exe) |

## What was NOT executed
| Feature | State |
|---|---|
| `plugin/*.cpp, *.hpp` as a Windows binary | **Syntax-checked only.** Never compiled to a DLL, never loaded. Expect to need small fixes on the first real compile |
| Custom DFF/TXD loading, pickup model registration (`CFileLoader::LoadObject` path, TXD slot, stream-state marking) | **Implemented, untested.** Built from disassembly + re3 source |
| World pickups (`GenerateNewOne`), real pickup collection through `WeaponForModel`/`GiveWeapon` hooks | **Implemented, untested** |
| PNG icon in the HUD (RenderWare image -> raster -> texture, `CHud::Sprites[type]` swap) | **Decoder host-tested; engine integration implemented, untested** |
| OGG sound in the game (`PlayOneShot` interception + waveOut mixer) | **Decoder and mixer math host-tested; hook and device output implemented, untested** |
| Empty-magazine click (`CWeapon::Fire` hook) | **Implemented, untested.** Depends on the engine calling `Fire` on an empty weapon (not verified) |
| Animations by name from the std ped group | **Implemented, untested** |
| Custom IFP animations appended to the std group | **Implemented, untested, highest risk** (engine-internal array manipulation; guarded by capacity checks that ARE host-tested) |
| Save/load persistence in the real game | **Logic host-tested; hooks implemented, untested** |
| Missions / `main.scm` compatibility, NPC side effects, controller input | **Not tested** |

## Checklist to run in-game (please send back `weaponloader.log`)
1. `python tools/verify_exe.py gta3.exe` must print ALL OK.
2. Build, install (see `INSTALL_GUIDE.md`). Start the game; the log should say `all hooks installed`, then per weapon: `model ... loaded`, `icon ... texture created`, `OGG decoded`, `registry: 17 definitions (4 custom)`.
3. New game, give yourself a pistol, press **F12**: four pickups appear at your feet. Walk onto the Desert Eagle: log `slot 2: 'desert_eagle' replaced 'original_pistol'`; HUD icon, held model, stats and sounds change. Walk onto the Colt Python: it replaces the Deagle. Collect a vanilla pistol pickup: it replaces the Python.
4. Save in a safehouse, quit, load: log `sidecar ... written`, then `load: restoring slot 2 = 'desert_eagle'`; the Deagle is still your pistol.
5. Cycle weapons with your normal keys / pad: the pistol slot appears once.
