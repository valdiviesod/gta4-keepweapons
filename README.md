# gta4-keepweapons

An ASI plugin for **GTA IV: The Complete Edition (1.2.0.59)** that keeps your weapons and ammo when you are **Wasted**, and optionally when you are **Busted**. It works in IV, TLAD and TBoGT.

> ✅ **Status: tested in-game.** It works on GTA IV CE 1.2.0.59 with FusionFix under Proton (Linux): weapons and ammo are restored after Wasted.

## Requirements
- GTA IV Complete Edition **1.2.0.59** (Steam)
- [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix), or any Ultimate ASI Loader (`dinput8.dll`)
- It does **not** need ScriptHook.
- It works on Windows and on Linux/Steam Deck through Proton. If Proton does not load the plugin, add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the game's launch options.

## Install
1. Download `KeepWeapons.asi` and `KeepWeapons.ini` from [Releases](../../releases).
2. Copy both files to `GTAIV/plugins/`.

## Config (`KeepWeapons.ini`)
```ini
[MAIN]
KeepOnDeath=1   ; restore weapons after Wasted
KeepOnArrest=1  ; restore weapons after Busted
KeepEmptyWeapons=1  ; keep a weapon in the inventory with 0 ammo instead of losing it
RestoreOnStrip=1    ; restore the loadout if every weapon is removed at once (mission death/retry)
```

## How it works
- The plugin patches one 5-byte call to `CGameProcess` in the main loop, so its code runs every frame on the game thread. It only runs when the game is not paused, and it always calls the original function afterwards.
- It reads natives from the game's own native table by hash, the same way FusionFix does. It does **not** use hard-coded addresses.
- While the player is alive, it records a snapshot of weapon slots 1–11 (weapon ID and ammo) and the current weapon.
- When the player dies or is arrested, that snapshot is kept. After the player respawns and the screen fades in, plus about 1.5 s, the plugin restores the weapons: `REMOVE_ALL_CHAR_WEAPONS`, then `GIVE_WEAPON_TO_CHAR` and `SET_CHAR_AMMO` for each weapon, then `SET_CURRENT_CHAR_WEAPON`. Removing everything first prevents duplicated ammo.
- **Keep empty weapons (v0.2.0):** GTA IV removes a weapon when its ammo reaches 0. Each frame the plugin compares the loadout with the previous frame. If a weapon disappeared after having 60 or fewer rounds left, the plugin adds it back with 0 ammo, so it stays in your inventory and you can refill it at Ammu-Nation. If every weapon disappears at once, the plugin treats it as a mission script taking them away and does nothing. If the game drops the same weapon again more than 3 times in 5 seconds, the plugin stops re-adding it. Empty weapons are also kept through death and arrest.
- **Mission death and retry (v0.3.0):** sometimes a mission removes all your weapons when it fails, even before the game marks you as dead. If every weapon disappears at once while you are alive, the plugin keeps your last full loadout, with its ammo, and gives it back once the screen fades in. It does this at most 2 times per minute, so missions that need you unarmed still work.
- **Fail-safe:** every pattern must match exactly once, and every native must resolve. If anything fails, the plugin patches nothing and stays inert.

## Logging
The plugin writes `KeepWeapons.log` next to the `.asi`:
- the number of matches for each pattern
- `init: OK` or `init FAILED: ...`
- `natives: all resolved`
- `down: cause=...` and `restore: ... weapons=N`
- `empty: weapon N ran dry ..., kept with 0 ammo`

## Build (Linux, no root)
```bash
./build.sh --fetch-zig --test
```
This downloads Zig 0.13.0 (sha256-verified) into `./tools`. It cross-compiles a 32-bit Windows DLL (`x86-windows-gnu`) and runs the pattern scanner's unit tests.

## Known limitations
- It only keeps weapons and ammo, not armour.
- If you load a save while the player is dead or being arrested, the old snapshot may be applied to the loaded save.
- The timing values (1.5 s delay, 20 s fade fallback, 180 s timeout) are guesses.

## Credits
- The game patterns, native-table lookup and call-context layout are based on [ThirteenAG/GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) (commit `cad0970e`): `source/dllmain.cpp`, `source/comvars.ixx` and `source/natives.ixx`.
- The native hashes were cross-checked against [Zolika1351/iv-sdk](https://github.com/Zolika1351/iv-sdk).

## License
GPL-3.0 (derived from FusionFix, which is GPL-3.0).
