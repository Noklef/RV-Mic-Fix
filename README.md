# RV Voice Fix

C++ UE4SS test mod for RV There Yet? (build CL-21945, Unreal 5.6). All
controls are in the game.

1. **Proximity voice:** the host sometimes hears a player at full volume at any
   distance. The mod keeps each remote talker registered and restarts voices
   that play without distance falloff (host only).
2. **Per-player volume:** 0-200% and mute per remote player, saved.
3. **Microphone switch:** change the microphone without a restart, saved and
   re-applied.

Status: test build. Volume, mute, the overlay and the Settings tab work in the
game (the tab's slider drag is not yet tested). The proximity repair and
transmitted audio after a microphone switch are not verified. See [STATUS.md](docs/STATUS.md).

## Compatibility

The mod disables itself unless both hashes match:

| File | SHA-256 |
| --- | --- |
| `Ride-Win64-Shipping.exe` (CL-21945) | `8c229aded5833756b124e9bbad59871b4417f3a7af26b55f194cd448df5537da` |
| `UE4SS.dll` (experimental 3.0.1-1183-gcdbc0b66) | `c273e7228687b647226fd63d7ca65888d313ece6be18996ec1320d00a5efe1f8` |

Use exactly UE4SS **3.0.1-1183-gcdbc0b66** from the
[experimental releases](https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/experimental-latest).
That tag moves. Stable 3.0.1 does not start in this game. The loader is not
included.

## Get the mod

Each push runs the **Build mod** workflow. Download
`RVVoiceFix-0.3.0-test.zip` from the run's Artifacts in
[Actions](https://github.com/Noklef/RV-Mic-Fix/actions/workflows/build.yml)
(kept 14 days). For a local build, see [DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Install

1. Exit the game. Install the pinned UE4SS.
2. Copy the ZIP's `RVVoiceFix` folder into `ue4ss\Mods`. Or, after a local
   build (`out\dist\RVVoiceFix`), run:

   ```powershell
   .\scripts\Install-Mod.ps1 `
     -ModsPath 'D:\Games\Steam\steamapps\common\Ride\Ride\Binaries\Win64\ue4ss\Mods' `
     -GameExecutable 'D:\Games\Steam\steamapps\common\Ride\Ride\Binaries\Win64\Ride-Win64-Shipping.exe'
   ```

   The installer checks all hashes. It refuses an existing `RVVoiceFix` or
   old `RVProximityFix` folder; remove those first. It never changes UE4SS.
3. If `Mods/mods.txt` disables the mod, set `RVVoiceFix : 1`.
4. Start the game. `RVVoiceFix/RVVoiceFix.log` must show `Loaded v0.3.0-test`
   and `Game-thread engine-tick callback is running`.

To uninstall, exit the game, remove the `RVVoiceFix` folder and any
`RVVoiceFix : 1` line you added.

## Use

| Key | Action |
| --- | --- |
| F7 | Open or close the overlay |
| Esc | Close the overlay |
| F8 | Write a snapshot to `RVVoiceFix.log` |
| F9 | Fix now (host only) |

The same controls are in **Settings -> Voice Fix**.

- **Microphone:** lists the Windows capture devices and marks the one in use.
  Click one to switch. "Windows default" means the default at the time of the
  click. The mod never changes the Windows default. A device connected after
  the game started can need a game restart.
- **Players:** distance, voice state, volume and mute for each remote player.
  **POSITIONAL** is correct. **GLOBAL** is the fault: full volume everywhere.
- **Proximity fix:** Auto-fix, Fix now, Write log, Details.

Settings are in `RVVoiceFix/RVVoiceFix.ini`, keyed by Steam ID (or by name if
the game gives no ID). Players you never change keep the game's volume.

A log line does not prove that the repair works; only audible falloff does.
Test procedure: [TESTING.md](docs/TESTING.md).
