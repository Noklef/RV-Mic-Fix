# RV Voice Fix

Experimental **C++ UE4SS test mod** for RV There Yet? build CL-21945 / Unreal 5.6.
It fixes three problems, with all controls drawn **inside the game** (F7):

1. **Proximity voice:** the host sometimes hears one player at full volume at
   any distance. The mod keeps each remote player's voice registered and
   restarts voices that play without distance falloff.
2. **Per-player volume:** 0-300% and mute for each remote player, saved.
3. **Microphone switch without restart:** pick a microphone in-game. The choice
   is saved and re-applied if the game reopens capture.

**Build and local checks pass. The in-game overlay, the repair, the volume
control and transmitted microphone audio are not verified in the game.** This is
a test build. See [design](docs/DESIGN.md) and [status](docs/STATUS.md).

The installed mod is entirely C++. PowerShell helpers build/package/install it;
there is no Lua bridge or Lua runtime mod. The old Lua prototype is retained in
Git history at `ae2c1b7` and is no longer packaged.

## Compatibility

The build and mod enforce these SHA-256 identities:

| File | SHA-256 |
| --- | --- |
| `Ride-Win64-Shipping.exe` (CL-21945) | `8c229aded5833756b124e9bbad59871b4417f3a7af26b55f194cd448df5537da` |
| `UE4SS.dll` (experimental 3.0.1-1183-gcdbc0b66) | `c273e7228687b647226fd63d7ca65888d313ece6be18996ec1320d00a5efe1f8` |

Use the exact **3.0.1-1183-gcdbc0b66** experimental build from the official
[UE4SS releases](https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/experimental-latest).
The experimental tag moves; a different build is not automatically compatible.
Stable 3.0.1 failed to initialize in the supported game build (engine version,
GUObjectArray and FText signature scan failures).
The loader is a separate dependency and is not included. This mod uses narrow,
handwritten ABI declarations, checked against the pinned DLL's exports and
public loader header. Compilation does not establish runtime ABI compatibility.

## Build and package

Every branch push runs the **Build mod** workflow on GitHub's Windows runner.
After a successful run, open [Actions](https://github.com/Noklef/RV-Mic-Fix/actions/workflows/build.yml),
select the run for your branch/commit, and download `RVVoiceFix-0.3.0-test.zip`
from **Artifacts**. No CMake or C++ tools are needed on your computer to use it.
Artifacts are retained for 14 days. You still need the pinned UE4SS loader to
run the mod.

The workflow checks formatting, runs static analysis, builds the DLL, and runs
native checks before uploading the ZIP. It can also be started manually.
It needs no repository secrets or self-hosted runner.

See [development instructions](docs/DEVELOPMENT.md) for optional local builds.
Generated files stay under `out/`; the local ZIP is
`out/dist/RVVoiceFix-0.3.0-test.zip`.

## Install for testing

Exit the game. Install the pinned UE4SS build using its own instructions first.
Use the existing Mods directory of that installation:

From the ZIP, copy its `RVVoiceFix` folder into that Mods directory. From the
repository, the checked installer is available below:

```powershell
.\scripts\Install-Mod.ps1 `
  -ModsPath 'D:\Games\Steam\steamapps\common\Ride\Ride\Binaries\Win64\ue4ss\Mods' `
  -GameExecutable 'D:\Games\Steam\steamapps\common\Ride\Ride\Binaries\Win64\Ride-Win64-Shipping.exe'
```

The installer verifies game, loader, and mod hashes. It refuses an existing
`RVVoiceFix` installation or a remaining `RVProximityFix` Lua installation.
Back up and remove the old mod yourself before installing. The helper never
installs or changes UE4SS. The included `enabled.txt` enables this mod; if your
`Mods/mods.txt` explicitly disables it, set `RVVoiceFix : 1`.

Check `RVVoiceFix/RVVoiceFix.log` for `Loaded v0.3.0-test` and `Game-thread
engine-tick callback is running` before testing.

## Controls

| Key | Action |
| --- | --- |
| F7 | Open/close the in-game overlay (mouse controls, game input paused) |
| Esc | Close the overlay |
| F8 | Write a session snapshot to `RVVoiceFix.log` |
| F9 | Fix now: re-register talkers and restart global voices (host only) |

The overlay has three sections:

- **Microphone:** the device the game's voice capture uses now, and the Windows
  capture devices. Click one to switch. The mod never changes Windows defaults.
  "Windows default" resolves the current default when you click it, and it is
  not re-applied automatically. A device connected after startup may need one
  game restart to enter the engine's device map.
- **Players:** each remote player with distance, voice state, a 0-300% volume
  slider and Mute. **POSITIONAL** means the voice falls off with distance.
  **GLOBAL** is the bug: a voice heard at full volume everywhere.
- **Proximity fix:** Auto-fix, Fix now, Write log, and Details (per-player
  talker and stream data). Repair changes nothing on a client.

Settings are saved in `RVVoiceFix/RVVoiceFix.ini`. Volume is keyed by Steam ID
(`steam <percent> <muted> <id> <name>`), or by display name when the game
reports no ID (`player <percent> <muted> <name>`). An older name-only line moves
to the Steam ID the first time that player is seen. Players you never adjust
are left at the game's volume.

**A log line is not proof that spatial audio is repaired.** Follow
[the live test procedure](docs/TESTING.md). Uninstall by exiting the game,
removing only `RVVoiceFix`, and removing any explicit enablement entry you added.

See [native evidence and limitations](docs/NATIVE-EVIDENCE.md).
