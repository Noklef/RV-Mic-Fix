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

- MSVC x64 `/W4 /WX /analyze` build passes. 130 native checks pass; nine real
  capture devices enumerated.
- All 34 imports were checked against the installed pinned `UE4SS.dll`
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

## Seventh in-game run (Voice Fix tab, overlay rows), single player

- The tab appears after Keybinds with the game's button style and the label
  "Voice Fix"; the page showed the overlay rows. User: "It works!"
- Request: make the page look like the game's own settings rows.
- Eighth run (game rows, build B7480061): the page showed the game's
  stepper and button rows. User request: group the rows under headings.

## Game update CL-21905 (30 October 2026)

- The update changed `Ride-Win64-Shipping.exe`; UE4SS is unchanged. The mod
  logged `DISABLED: executable or UE4SS SHA-256 does not match`.
- The five microphone RVAs were found again (static byte comparison, see
  [NATIVE-EVIDENCE.md](NATIVE-EVIDENCE.md)); field offsets are unchanged.
  The new hash is pinned (build 63687DEB).
- New read-only `MIC-DIAG` log line (TESTING step 2d): the game's Redpoint
  input device beside the native capture device. It decides whether
  microphone switching can use the game's reflected setter instead of
  addresses.

## Game update CL-21945 (31 October 2026)

- A second update (EXE `8c229ade...`) disabled the mod again. Same
  relocation: ChangeDevice and the vtable +0x450, manager +0x4000, name hash
  and map lookup unchanged ([NATIVE-EVIDENCE.md](NATIVE-EVIDENCE.md)).

## Ninth in-game run (CL-21945, host with Heirthena)

- `Exact executable and loader hashes accepted`. Steam IDs, volume (300% =
  7.50) and mute applied.
- `MIC-DIAG game <unavailable>` on every line: the Redpoint path did not run
  (a missing controller or class; the line does not say which).
- The Voice Fix tab was added in the menu, then a later attach logged "the
  menu has no tab to copy". That failure stops the tab for the session (also
  seen once before the update, with no other player). Fixed (not yet tested):
  a menu without tabs is "not ready", logged once and retried; the mod tab is
  no longer removed before a re-add can succeed (build 63687DEB). MIC-DIAG
  now names the missing piece.
- Tenth run: the tab survives menu changes. Its volume slider would not drag:
  the game entry (`WG_OptionEntryBar`) runs its `AnalogSlider` 0-1 and passes
  `StepSize` unscaled, so a step of 5 locked it. Now 5/MaxPercent. MIC-DIAG said
  `<no OnlineHelpers>`: `ue::live()` refuses default objects; now a null check.
  Both fixed in build 63687DEB, untested.

## Queue

0. Game updates CL-21905 and CL-21945 (build 63687DEB): the mod loads on
   CL-21945 (ninth run); a microphone switch is still untested;
   MIC-DIAG (TESTING step 2d) decides the reflected microphone path.
1. Settings "Voice Fix" tab, grouped (in build 63687DEB): the page is a scroll
   box with the game's section heading (`WG_HeadlineDivider`) for each of
   Microphone, Players (only with remote players) and Troubleshooting. Under
   each heading is a copy of the Gameplay `RGListView` (style copied with
   `CopyCompleteValue`, import 34) in a size box as tall as its rows. The row
   height is measured from the row widgets on screen. Rows: Device stepper
   (applies 0.8 s after the last step, names cut to 26 characters), Device
   list (Refresh); per player a volume slider and an indented Mute stepper
   (`IsSubOption`); Proximity auto-fix, Fix now, Write log. Diagnostics stay
   in the F7 overlay (user decision). Untested; the user runs
   [TESTING.md](TESTING.md) step 2c. Unknowns: whether `IsSubOption` indents
   the row, the button name `WG_SignButton_111`. Known: an unplugged saved
   microphone shows as "Windows default". The tab appears up to 1 s after the
   menu opens (1 s poll); user decision: leave as is (a construct hook is
   possible later).
2. Settings -> Sound button: moved above "Input Device", left-aligned. The
   user runs step 2b. Keep or remove it once the tab works: user decision.
3. Pending user decision: default auto-fix OFF; leave the PlayerState-root
   attach alone in the repair; commit.
4. Steam ID keys: verified in the ninth run (`PLAYER ... [steam:<id>]` and a
   `steam` line in `RVVoiceFix.ini`).
5. Volume cap raised to 300% (user request: 200% did not sound like double).
   Ninth run: 300% applied (multiplier 7.50) but sounds no louder than 200%
   (5.00): an engine volume cap is likely. User decision: cap back at 200%.

## Next action

The user launches the updated game and checks step 1 (`Exact executable and
loader hashes accepted`), step 2 (microphone switch on CL-21905), step 2d
(MIC-DIAG), and step 2c (Voice Fix tab), then sends `RVVoiceFix.log`.
