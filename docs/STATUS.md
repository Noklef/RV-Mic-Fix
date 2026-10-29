# Status - 29 October 2026 (Australia/Sydney)

Version 0.3.0-test. History: [handoff](history/HANDOFF-2026-10-29.md),
[design](DESIGN.md).

## User requirements

1. Fix proximity voice: the host sometimes hears one player at any distance.
2. Add per-player volume control.
3. Switch microphone without restarting the game.

All UI and diagnostics must be **in-game**. The v0.2.1 F7 Win32 window did not
meet this requirement and was removed.

## First in-game run (v0.2.1-test, single player)

Loading, the engine-tick callback, F7 and four native microphone switches
worked without fault. Every proximity attempt stopped at `VoiceSettings
reflection is unavailable`, so no earlier build ever repaired a talker.
Details: [DESIGN.md](DESIGN.md#evidence-from-the-first-in-game-run-v021-test).

## Second in-game run (v0.3.0-test, HUD canvas), single player

Input mode and cursor worked after the `Method` CDO fix. The overlay never
appeared: in one second, 73,455 `ProcessEvent` calls and zero `ReceiveDrawHUD`
calls. The game uses the plain engine `HUD`, so the HUD-canvas approach was
dropped. The user chose UMG widgets ("UMG for now"); moving the controls into a
game menu is planned later.

## Implemented in 0.3.0-test

- In-game overlay as a UMG widget tree created through reflection (plain
  `UUserWidget`, native widgets via `SpawnObject`), added to the viewport. Clicks
  and slider moves are polled each tick. UI-only input and cursor while open.
  The overlay closes if the game removes the widget.
- Microphone list with the device in use, click to switch, saved choice,
  automatic re-apply (rate-limited, stops after three failures).
- Per-player list: distance, POSITIONAL/GLOBAL state, 0-200% slider, mute,
  saved in `RVVoiceFix.ini`.
- Proximity repair every 2 s on the host: garbage talkers excluded, settings
  restored, re-registration, bounded restart of voices playing without
  attenuation. Struct members are read through `FStructProperty::GetStruct`,
  which fixes the v0.2.1 failure.
- Notices for repair events while the overlay is closed; F8 snapshot.
- Settings -> Sound "Input Device" shows the Windows default recording device
  (Redpoint voice chat is inactive; see NATIVE-EVIDENCE.md). While that menu
  exists, the mod rewrites the label to the device the voice capture uses
  (verified in game).
  The "Voice Level" meter and its device text belong to the game's preview
  `AudioCaptureComponent`, which always opens the Windows default and has no
  reflected device choice; the mod leaves that text unchanged, because it is
  true for the meter.
- Automatic snapshot on session, player or fault-count change; named
  `RESTARTED` lines; one `VOLUME` result line per slider change or mute.

## Local verification

- MSVC x64 `/W4 /WX /analyze` build passes. 114 native checks pass; nine real
  capture devices enumerated.
- All 33 imports were checked against the installed pinned `UE4SS.dll`
  (`-UE4SSDll`).

## Third in-game run (UMG), single player host

- Overlay renders: 38 widgets built, no failed UMG call, no `FText` release
  failure. Fonts, colours, layout, and the UI-only cursor all work.
- Button clicks work; the native microphone switch reopened capture on the
  chosen device, and the row showed IN USE - SAVED.
- Repeated reopens of one device (4-6 times, about 150 ms apart) were
  repeated clicks: a fourth run with one B1 click logged exactly one reopen.

## Fourth in-game run, host with one remote player (NutLord)

- Talker found and registered; settings restored (attach `CharacterMesh0`,
  attenuation `SA_VOIP`).
- Slider has no effect: every `VOLUME` line says "no voice stream yet". The
  only snapshot (at join) showed `streams=0`; no `VoipListenerSynthComponent`
  was matched to the player although `Ride.log` shows remote voice decoding.
- F8 `VOICE-DIAG` (pressed while NutLord talked): one
  `VoipListenerSynthComponent`, live, same world, playing, **no attach
  parent**. Its inner `AudioComponent_0` is attached to `DefaultSceneRoot`,
  attenuation `SA_VOIP`, `VolumeMultiplier` 2.50.
- Cause: the mod matched streams by the synth's own parent, and set the
  multiplier to percent/100 (100% would have been 40% of the game level).
  Fixed: match and GLOBAL check use the inner component; volume scales from
  the game's own multiplier. Needs a retest.

## Fifth in-game run (stream-match fix), host with NutLord, after a load

- Before the load: talker matched (`talkers=3`). After the load:
  `talkers=4 stale=0`, `talker=0` ("no matching talker"), so the overlay
  shows NO TALKER while the player talks.
- The stream (inner `AudioComponent`, parent `DefaultSceneRoot`,
  `SA_VOIP`, 2.50) is still unmatched: `DefaultSceneRoot` is not owned by the
  player's pawn, and without a talker there is no attach parent to compare.
- F8 `VOICE-DIAG` now adds full names for each player (state, pawn), each
  talker (state, owner, attach component and its owner), and the stream
  parent's owner, to find the ownership chain after a load.

## Sixth in-game run (full-name diagnostics), before and after a load

- Talkers are components of each `BP_PlayerState_C`; `ComponentToAttachTo`
  is that PlayerState's `DefaultSceneRoot`. `CachedPlayerState` reads
  `<none>` on every talker.
- The voice stream's inner `AudioComponent` is attached to the remote
  PlayerState's `DefaultSceneRoot` (owner: the PlayerState, not the pawn).
- Before the load: one talker on NutLord's PlayerState; stream matched,
  `VOLUME NutLord 100%: applied` (multiplier 2.50 = game level).
- After the load: new PlayerStates; NutLord's has two talkers on the same
  `DefaultSceneRoot`. The tie made the talker ambiguous (NO TALKER), and the
  stream had no talker to match through.
- Fixed: a stream also matches by its parent's owner == the player's
  PlayerState; tied talkers that share one attach component count as one.
  Verified in game (seventh run): after a load `talker=1`, `stream playing`,
  positional, multiplier 5.00 at 200% (2.50 x 2); mute and unmute applied.

## Not verified (needs the game)

- The stream restart on the inner component.
- Reflected names resolve: `CachedPlayerState`, `AttachParent`,
  `AudioComponent`, `VolumeMultiplier`, `GetDistanceTo`,
  `VoipListenerSynthComponent`.
- The GC hypothesis for the proximity fault, and an audible repair.
- Transmitted audio after a microphone switch.

## Queue

1. Settings -> Sound "RV Voice Fix" button: built and deployed; the user runs
   [TESTING.md](TESTING.md) step 2b. Input after closing the overlay from the
   menu is unverified (the existing path restores game-and-UI input).
2. Pending user decision: default auto-fix OFF; leave the PlayerState-root
   attach alone in the repair; commit.
3. Steam ID keys (123 native checks) need an in-game check: the `PLAYER` line
   shows `[steam:<id>]` and `RVVoiceFix.ini` gets a `steam` line.

## Next action

The user plays with another player, presses F8 while that player talks, and
sends `RVVoiceFix.log`.
