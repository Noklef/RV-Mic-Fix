# RV Voice Fix

Experimental **C++ UE4SS test mod** for RV There Yet? 1.3.0.20702 / Unreal 5.6.
It provides a runtime microphone selector and attempts to restore host-side
proximity voice after checkpoint resets.

**Build and local UI checks pass. Game loading, transmitted microphone audio,
and audible multiplayer repair are not verified.** This is a test build.
The microphone dropdown uses an F7 window for testing. Integration into the
game's **SoundOptions** menu remains pending.

The installed mod is entirely C++. PowerShell helpers build/package/install it;
there is no Lua bridge or Lua runtime mod. The old Lua prototype is retained in
Git history at `ae2c1b7` and is no longer packaged.

## Compatibility

The build and mod enforce these SHA-256 identities:

| File | SHA-256 |
| --- | --- |
| `Ride-Win64-Shipping.exe` (1.3.0.20702) | `12b24146b30d550fe47a4179c7b05d1f21b69f058524e0bba4b41397e3e66221` |
| `UE4SS.dll` (zDEV 3.0.1-1183-gcdbc0b66) | `c273e7228687b647226fd63d7ca65888d313ece6be18996ec1320d00a5efe1f8` |

Use that exact official [UE4SS build](https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/experimental-latest).
The experimental tag moves; a newer asset with the same tag is insufficient.
The loader is a separate dependency and is not included. This mod uses narrow,
handwritten ABI declarations, checked against the pinned DLL's exports and
public loader header. Compilation does not establish runtime ABI compatibility.

## Build and package

Requires Visual Studio 2022 Build Tools with the x64 C++ toolchain and Windows
SDK. The current build uses MSVC 14.44, C++20, Release `/MD`, iterator debug level 0.

```powershell
.\scripts\Build-Native.ps1 -UE4SSDll 'C:\path\to\pinned\UE4SS.dll' -Test
.\scripts\Build-Package.ps1 -UE4SSDll 'C:\path\to\pinned\UE4SS.dll'
```

The first command produces `dist/RVVoiceFix/dlls/main.dll`. The second builds,
runs local checks, and produces `dist/RVVoiceFix-0.2.0-test.zip`. Native checks
exercise the real Windows dropdown, device enumeration, command queue, repair
scheduling, and basic guards. They do not invoke game capture functions.

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

Start in windowed or borderless mode. Check `RVVoiceFix/RVVoiceFix.log` for
`Loaded v0.2.0-test` and `Game-thread callback registered` before testing.

| Control | Action |
| --- | --- |
| F7 | Open/hide the microphone dropdown and test controls |
| Apply | Queue a capture-device change on the game thread |
| Refresh | Refresh the Windows device list; does not reopen capture |
| F8 / Diagnose | Write a current-world/player/talker snapshot |
| F9 / Repair | Queue three repair attempts, even with automatic repair off |
| F10 / Toggle auto | Toggle automatic repair and cancel pending attempts |

Select a microphone and click Apply while in a voice session. The default option
resolves the **current** Windows default at Apply time. The mod does not change
Windows defaults. Selection is not saved or automatically reapplied; use Apply
again if the game recreates capture. Close the game's microphone test screen if
the mod reports multiple initialized captures. A device connected after startup
may require one restart to enter the engine's cached device map.

Automatic repair starts enabled. Changes in the host's world or remote player
identities schedule attempts after 3, 6.5, and 10 seconds. A reset that preserves
all observed identities may need F9. The repair matches existing remote talkers,
restores stale attachment/missing attenuation, and registers them again. It
preserves valid attenuation and source effects. It does not create missing
talkers or force an existing remote playback stream to restart.

**A registration log is not proof that spatial audio is repaired.** Follow
[the live test procedure](docs/TESTING.md). Uninstall by exiting the game,
removing only `RVVoiceFix`, and removing any explicit enablement entry you added.

See [native evidence and limitations](docs/NATIVE-EVIDENCE.md) and
[current status](docs/STATUS.md).
