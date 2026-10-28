# gta4-keepweapons

An ASI plugin for **GTA IV: The Complete Edition (1.2.0.59)** that keeps your weapons and ammo when you are **Wasted**, and optionally when you are **Busted**. It works in IV, TLAD and TBoGT.

**Target:** GTA IV Complete Edition 1.2.0.59 with FusionFix under Proton (Linux). The current build compiles and has been launched in-game on the setup documented below.

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
KeepEmptyWeapons=1  ; prevent a weapon being discarded at zero by replenishing the final round
RestoreOnStrip=1    ; restore the loadout if every weapon is removed at once (mission death/retry)
GrantRequestedLoadout=0 ; one-shot: grant Pistol, Micro-Uzi and Pump Shotgun at max ammo
```

## How it works
- The plugin patches one 5-byte call to `CGameProcess` in the main loop, so its code runs every frame on the game thread and always calls the original function afterwards. It checks pause flags when the current game build exposes matching instructions; otherwise it continues without those optional checks.
- It reads natives from the game's own native table by hash, the same way FusionFix does. It does **not** use hard-coded addresses.
- While the player is alive, it records a snapshot of weapon slots 1–11 (weapon ID and ammo) and the current weapon.
- When the player dies or is arrested, that snapshot is kept. After the player respawns and the screen fades in, plus about 1.5 s, the plugin restores the weapons: `REMOVE_ALL_CHAR_WEAPONS`, then `GIVE_WEAPON_TO_CHAR` and `SET_CHAR_AMMO` for each weapon, then `SET_CURRENT_CHAR_WEAPON`. Removing everything first prevents duplicated ammo.
- **Keep weapons when nearly empty:** GTA IV removes a weapon when its total ammo reaches 0; setting ammo to 0 does not preserve it. With `KeepEmptyWeapons=1`, the plugin raises a weapon's total ammo to 2 when it reaches 1, before the game's next frame can remove it. This keeps the weapon in the inventory and avoids the discard animation. It leaves other ammo counts alone. Since the game cannot represent an owned weapon at exactly zero ammo, the last round is effectively replenished; disable this option for standard ammo depletion.
- **Requested starting loadout:** setting `GrantRequestedLoadout=1` gives weapon IDs for the Pistol, Micro-Uzi and Pump Shotgun their game-reported maximum ammo the first time the player is alive. The option resets itself to 0 after that one grant. The change remains in the current session; use an in-game save to keep it in the save file.
- **Mission death and retry (v0.3.0):** sometimes a mission removes all your weapons when it fails, even before the game marks you as dead. If every weapon disappears at once while you are alive, the plugin keeps your last full loadout, with its ammo, and gives it back once the screen fades in. It does this at most 2 times per minute, so missions that need you unarmed still work.
- **Fail-safe:** required game patterns must resolve (the `CGameProcess` call site must be unique) before any code is patched, and every native must resolve before gameplay logic runs. Pause-flag patterns are optional because some Complete Edition builds use different instructions. If a required pattern fails, the plugin patches nothing and stays inert.

## Logging
The plugin writes `KeepWeapons.log` next to the `.asi`:
- resolved game patterns and globals
- `init: OK` or `init FAILED: ...`
- `natives: all resolved`
- `down: cause=...` and `restore: ... weapons=N`
- `empty: weapon N reached 1 total ammo; kept above zero`

## Build (Linux, no root)
```bash
./build.sh --fetch-zig --test
```
This downloads Zig 0.13.0 (sha256-verified) into `./tools`. It cross-compiles a 32-bit Windows DLL (`x86-windows-gnu`) and runs the pattern scanner's unit tests.

## Known limitations
- It only keeps weapons and ammo, not armour.
- If you load a save while the player is dead or being arrested, the old snapshot may be applied to the loaded save.
- The timing values (1.5 s delay, 20 s fade fallback, 180 s timeout) are guesses.

## Configuración aplicada en este equipo

Fecha: 2026-10-20. Instalación de Steam: `/home/v4ld1/.local/share/Steam/steamapps/common/Grand Theft Auto IV/GTAIV/`. El plugin instalado está en `plugins/KeepWeapons.asi`; su configuración está en `plugins/KeepWeapons.ini`. Requiere FusionFix/Ultimate ASI Loader.

### Mod e inventario
- `KeepEmptyWeapons=1`: al llegar al último cartucho, el mod mantiene el arma con munición total 2 para evitar que GTA IV la quite al llegar a cero. El juego no conserva un arma poseída con exactamente 0 de munición.
- Se solicitó una sola entrega de pistola, Micro-Uzi y escopeta Pump con la munición máxima del juego. La opción se restablece tras entregarlas; guarda la partida dentro del juego para conservar el inventario en ese save.
- Validación: compilación y pruebas del escáner de patrones completadas; el plugin se cargó y concedió las tres armas en una sesión real. No se documentó una prueba de disparo hasta agotar el último cartucho.

### Resolución y gráficos
`GTAIV/commandline.txt` fuerza pantalla completa a **1920×1080, 180 Hz**, la resolución y frecuencia máximas del monitor externo conectado. También fija `texturequality=2`, `renderquality=4`, `shadowquality=4`, `shadowdensity=16`, `viewdistance=99`, `detailquality=99` y `vehicledensity=100`, con `-norestrictions -nomemrestrict`. En `GTAIV/plugins/GTAIV.EFLC.FusionFix.ini` se activaron sombras de alta resolución, reflexión MSAA calidad 8 y sun shafts densidad 1.0.

Estos valores priorizan el máximo visual; distancia y densidad al tope pueden reducir rendimiento o saturar el tráfico según la escena y el equipo.

### Ventana de Rockstar en Omarchy
En `~/.config/hypr/hyprland.lua`, las ventanas auxiliares de Steam/Rockstar se envían al workspace especial oculto y sin foco; la ventana titulada `GTAIV` se asigna al workspace 5 para que el juego siga visible. La configuración se recargó y `hyprctl configerrors` no reportó errores.

## Credits
- The game patterns, native-table lookup and call-context layout are based on [ThirteenAG/GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) (commit `cad0970e`): `source/dllmain.cpp`, `source/comvars.ixx` and `source/natives.ixx`.
- The native hashes were cross-checked against [Zolika1351/iv-sdk](https://github.com/Zolika1351/iv-sdk).

## License
GPL-3.0 (derived from FusionFix, which is GPL-3.0).
