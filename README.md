# WeaponLoader for GTA III (PC 1.0)

**WeaponLoader** is a data-driven .asi plugin for GTA III (PC 1.0) that adds an extensible, modular weapon system. Highly inspired by the architecture of Maxo’s Vehicle Loader (MVL), this plugin allows you to seamlessly add custom weapon categories, models, stats, custom animations, and OGG audio via external asset packs—**without replacing vanilla weapons or modifying original game files.**

By bridging engine gaps between RenderWare eras, it backports weapon behaviors and multi-variant slot structures from *Vice City* and *San Andreas* directly into Liberty City.

---

### Key Features

* **MVL-Style Asset Packs:** Drop custom weapon folders with an XML configuration file straight into the loader directory. Your vanilla game files stay pristine.
* **Same-Slot Variant Switching:** Adds advanced weapon replacement within the original 13 weapon categories (e.g., original pistol -> Desert Eagle -> Colt Python) instead of accumulating weapons in a single slot.
* **Custom DFF/TXD Registry:** Hooks the engine's IDE-line loader to register custom model IDs and textures seamlessly into the world pool.
* **Custom PNG HUD Icons:** Bypasses original TXD constraints with a built-in PNG decoder, injecting sharp HUD graphics directly into active slots.
* **Pure OGG Audio Path:** Uses a custom Vorbis decoder to stream raw PCM audio directly into a Win32 waveOut mixer for fire, reload, and empty click sounds—completely bypassing the WAV container overhead.
* **Advanced Animation Injections:** Appends custom IFP files to the engine's standard ped animation groups by name with safety checks against memory limits.
* **Isolated Save Persistence:** Writes dedicated sidecar files (`GTA3sfN.b.wl`) next to vanilla saves, keeping custom weapon stats safe without corrupting original game formats.
* **F12 Debug Spawner:** Includes a native development hotkey to instantly drop your custom pickup models directly at the player's feet for quick in-game testing.

---

### How to Install and Use

1. **Install the Plugin:** Place `weaponloader.asi` and the `weaponloader` folder directly into your main GTA III root installation directory. Ensure you have an ASI Loader (like Silent's ASI Loader) installed.
2. **Add Your Asset Packs:** Create a folder inside `weaponloader/` for your weapon. Place your matching `.dff`, `.txd`, `.png` icon, and `.ogg` sound files inside it along with your configuration `weapon.xml`.
3. **Verify Configuration:** Run `python tools/verify_exe.py gta3.exe` inside your folder to confirm your executable's structural compatibility.
4. **Debug Logs:** If a weapon fails to show up or skips a sound effect, check `weaponloader.log` in your main game directory for explicit layout warnings.

---

### Configuration File Layout (weapon.xml)

```xml
<weapon id="desert_eagle">
  <name>Desert Eagle</name>
  <slot>pistol</slot>
  <category>pistol</category>
  <stats damage="55" range="32" fireRate="450" reload="600" clipSize="7"/>
  <ammo pickupAmmo="21" maxAmmo="250"/>
  <assets>
    <model>m.dff</model>
    <pickupModel>p.dff</pickupModel>
    <txd>t.txd</txd>
    <icon>icon.png</icon>
  </assets>
  <audio volume="1.0">
    <fire>fire.ogg</fire>
    <reload>reload.ogg</reload>
    <empty>empty.ogg</empty>
  </audio>
  <animations anim1="WEAPON_hgun_body" anim2="" ifp="my.ifp" block="MyBlock"
              loopStart="" loopEnd="" frameFire="0.09" frame2Fire=""/>
  <spawns>
    <spawn x="814" y="-940" z="15" type="2" ammo="21"/>
  </spawns>
</weapon>
```

---

### Known Limitations

* **Shared NPC Stats:** CWeaponInfo layout fields are per weapon type. While a variant is active in the player's hands, NPCs using that same base weapon class will inherit its active stats and animations.
* **Save Dependency:** If you uninstall a custom weapon asset pack while its pickup still exists inside a saved world pool, loading that save file will crash. Collect or clear custom world pickups before removing a mod pack.
* **Sound Scope:** Custom audio layers replace only the player's direct shot (0x2F) and reload (0x30) audio events. NPC gunfire, melee, and environmental weapon sounds use vanilla structures.
