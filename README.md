# RV Proximity Fix

Experimental host-side UE4SS mod for **RV There Yet?**, targeting proximity voice
that works initially and loses direction/distance after a checkpoint reset.

**Status: prototype, not verified in a live game.** Automated tests exercise the
repair policy, engine adapter with mocked Unreal objects, and lifecycle scheduling.
They do not establish that this repairs the game's actual audio output.

## What it does

After player/world/talker construction or `ClientRestart`, the mod waits for the
new players to initialise and makes three bounded repair attempts. Each attempt:

1. Resolves the current viewport world and confirms a local host with authority.
2. Reads that world's current player roster and checks each pawn's PlayerState.
3. Matches remote VOIPTalkers using exact player ownership, an exposed cached
   PlayerState, or attachment ownership. Ambiguous/duplicate talkers are skipped.
4. Restores a missing/retired attachment to the current pawn's mesh or root.
5. Restores missing attenuation from the already loaded game asset `SA_VOIP`.
6. Calls `RegisterWithPlayerState` on the existing talker.

Valid game-selected attenuation and source effects are preserved. The mod does
not create replacement talkers, change packets, or modify the game's cooked
assets. It makes no raw memory-offset writes. A registration log records that a
call completed; it does not claim the audible result is fixed.

## Requirements

- Windows RV There Yet?; initial target is **1.3.0.20702 / Unreal 5.6**.
- A UE5.6-compatible [UE4SS build](https://github.com/UE4SS-RE/RE-UE4SS/releases)
  exposing **ExecuteInGameThreadWithDelay** and **ExecuteInGameThread**. The mod
  refuses to start if those functions are absent. Do not assume the old stable
  3.0.1 build has these capabilities.
- Begin testing with **only the host** running this mod.

UE4SS compatibility with this exact executable has not been tested. Its loader
is a separate dependency and is not included in this repository or ZIP. Follow
the selected build's own [installation instructions](https://docs.ue4ss.com/).

## Install

Exit the game and install a suitable UE4SS build first. Identify **that build's
actual Mods directory**; layouts differ. For this game it may be under:

```text
D:\Games\Steam\steamapps\common\Ride\Ride\Binaries\Win64\ue4ss\Mods
```

Copy `Mods/RVProximityFix` into that directory. Alternatively, from this repo:

```powershell
.\scripts\Install-Mod.ps1 -ModsPath 'D:\Games\Steam\steamapps\common\Ride\Ride\Binaries\Win64\ue4ss\Mods'
```

The helper copies only this mod into an existing Mods directory and refuses to
overwrite an existing RVProximityFix installation. It does not install UE4SS.

The included `enabled.txt` enables the mod through UE4SS's folder mechanism. If
your `Mods/mods.txt` explicitly disables `RVProximityFix`, update that entry to
`RVProximityFix : 1`. Confirm `[RVProximityFix] Loaded v0.1.0` appears in
`UE4SS.log` before testing.

## Controls

| Key | Action |
| --- | --- |
| F8 | Read-only snapshot of players, talkers, attachment and attenuation |
| F9 | Queue a manual repair burst, including when automatic repair is off |
| F10 | Toggle automatic repair for this game session; cancels queued repairs |

Automatic repair starts enabled. Edit
`Mods/RVProximityFix/Scripts/config.lua` **before launching the game** to change
keys, timings, or the startup setting. Restart the game after editing.

After a reset, allow about **10 seconds after the last lifecycle event** for all
attempts. Have remote players stop speaking briefly, then speak again; an
already active voice stream may retain old playback state until a new talkspurt.

For a controlled first test, set `AutoRepair = false`, reproduce the broken
state, press F8, then F9 and compare the audio. See [the test procedure](docs/TESTING.md).

## Logs and limitations

Look for `[RVProximityFix]` lines in the UE4SS log. `REGISTERED` means the call
completed. `SKIP` explains why an association or repair was refused. `NO_TALKERS`
means this engine-level route found no usable current talkers; the mod emits
available Blueprint schema information rather than guessing how to recreate one.

It cannot yet repair a completely missing talker or an unidentifiable transient
talker, and it does not forcibly restart native remote audio playback. If
registration succeeds but audio stays global, that is an unresolved result,
not evidence that the patch worked. A different game voice backend may also
require a separate implementation. See [investigation notes](docs/INVESTIGATION.md).

To uninstall, exit the game and remove only `RVProximityFix` from the UE4SS Mods
directory and any explicit RVProximityFix entry you added to `mods.txt`.

## Development

Node.js is used **only for tests**, not by the installed Lua mod.

```powershell
npm ci --ignore-scripts
npm test
.\scripts\Build-Package.ps1
```

The Lua test VM is pinned by `package-lock.json`. Tests use mocked engine objects,
including old worlds, duplicate talkers, authority loss, weak attachments,
registration errors, and repeated checkpoint events. The package is written to
`dist/RVProximityFix-0.1.0.zip` and contains the mod, documentation, and license.
