# GTA III Weapon Loader: Easy Guide

> **Status:** all six features are implemented in code, but the plugin has **never been compiled to a DLL or run in-game** (see `docs/TESTING_REPORT.md`). Back up your game folder first.
> It works with **GTA III PC version 1.0 only** (it refuses to run on any other version).

---

## 1. What gets compiled

You compile **one file only**: `plugin/plugin.cpp`.
It automatically includes the other three, so you don't list them. Keep the folder structure from the zip, because the includes use relative paths.

```
weaponloader/
├─ plugin/plugin.cpp          <- COMPILE THIS
├─ plugin/gta3_addrs.hpp      <- included automatically
├─ core/weapon_core.hpp       <- included automatically
└─ core/xml_mini.hpp          <- included automatically
```

The result is **`weaponloader.asi`**. It must be a **32-bit (x86)** build, because GTA III is a 32-bit game.

---

## 2. Compile it (choose ONE option)

### Easiest: no compiler installed
Upload this folder to a free GitHub repository, open **Actions → build → Run workflow**, then download the `weaponloader-asi` artifact (it contains `weaponloader.asi` and the `weaponloader` folder, ready to copy into the game folder).

### Easy: one click
Install Visual Studio Build Tools ("Desktop development with C++") or a 32-bit MinGW (e.g. w64devkit), then double-click **`build_asi.bat`**.

### Option A: Visual Studio (easiest on Windows)

1. Install **Visual Studio** with the workload **"Desktop development with C++"**.
2. Open **"x86 Native Tools Command Prompt for VS"** from the Start menu. It must say **x86**, not x64.
3. Go to the unzipped `weaponloader` folder:
   ```
   cd C:\path\to\weaponloader
   ```
4. Run:
   ```
   cl /LD /MT /EHsc /std:c++17 plugin\plugin.cpp /Fe:weaponloader.asi /link winmm.lib
   ```

### Option B: MinGW (32-bit / i686)

In the `weaponloader` folder run:

```
i686-w64-mingw32-g++ -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at -o weaponloader.asi plugin/plugin.cpp -lwinmm
```

If you get compile errors, copy the error text and send it to me so I can fix the code.

---

## 3. Install

1. **Back up** your GTA III folder (the one that contains `gta3.exe`).
2. Install an **ASI loader** (for example *Ultimate ASI Loader*): put its `dinput8.dll` next to `gta3.exe`. Skip this if you already have one.
3. Copy **`weaponloader.asi`** next to `gta3.exe`.
4. Copy the **contents** of `pack/weaponloader/` into a new folder named **`weaponloader`** next to `gta3.exe`.

Your game folder should look like this:

```
GTA3/
├─ gta3.exe
├─ dinput8.dll                      (ASI loader)
├─ weaponloader.asi                 (the file you compiled)
└─ weaponloader/
   ├─ loader.ini
   └─ weapons/
      ├─ desert_eagle/weapon.xml
      ├─ colt_python/weapon.xml
      ├─ m4/weapon.xml
      └─ ruger_556/weapon.xml
```

---

## 4. Weapon files you must supply
The example pack only contains `weapon.xml` files. Put your own files next to each XML (names are set inside the XML):

```
weaponloader/weapons/desert_eagle/
   weapon.xml
   desert_eagle.dff          held weapon model        (one clump, one atomic)
   desert_eagle_pickup.dff   pickup model (optional; the held model is used if absent)
   desert_eagle.txd          textures for both models
   icon.png                  HUD icon (non-interlaced PNG)
   fire.ogg  reload.ogg  empty.ogg     sounds (OGG Vorbis only)
```
Anything missing is skipped with a warning in `weaponloader.log`; the weapon still works with whatever is present. A weapon with
no model of its own cannot have world pickups (the loader logs this).

## 5. Test it
1. Run `python tools/verify_exe.py gta3.exe` first. It must say ALL OK.
2. Start the game. `weaponloader.log` should say `all hooks installed`, then list each model/icon/sound it loaded.
   `unsupported executable` or `signature check failed` means nothing was patched.
3. Start a **new game**. Press **F12**: one pickup of every custom weapon appears in a row at your feet. Walk onto them:
   - Desert Eagle replaces your pistol, Colt Python then replaces the Deagle, a normal pistol pickup replaces the Python.
   - Check the HUD icon, the held model, the shot/reload sounds and the clip size change with each one.
4. Save in a safehouse, quit to the menu, load that save: the log shows `load: restoring slot 2 = ...` and the same weapon is still in the slot.
5. Cycle weapons with your normal keys/pad. The pistol slot appears only once.
6. Instant-pickup keys (F6-F11, configurable in `weaponloader/loader.ini`) run the same replacement code without a pickup object.

Send me `weaponloader.log` (and any crash message) after the first run.

## 6. If something goes wrong
| Problem | What to do |
|---|---|
| Game does not start | Remove `weaponloader.asi`; check the ASI loader works alone. Compile errors or a crash on start: send me the text/log |
| No `weaponloader.log` | The ASI loader is not loading the plugin, or the file is in the wrong folder |
| `unsupported executable` / `signature check failed` | Not GTA III 1.0 (or a modified exe). Nothing was changed in memory |
| Weapon has no custom model/icon/sound | Look for `warn` / `failed` lines for that weapon id in the log |
| Crash when loading a save | A save that contains a custom weapon's pickup was loaded after the weapon was uninstalled |
| F-keys do nothing | `weaponloader/loader.ini` missing, or log lacks `all hooks installed` |
