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

## Not verified (needs the game)

- Slider `GetValue` and volume (needs another player).
- Reflected names resolve: `CachedPlayerState`, `AttachParent`,
  `AudioComponent`, `VolumeMultiplier`, `PlayerNamePrivate`,
  `GetDistanceTo`, `VoipListenerSynthComponent`.
- The GC hypothesis for the proximity fault, and an audible repair.
- Transmitted audio after a microphone switch.

## Next action

The user runs [TESTING.md](TESTING.md) steps 1-4 and sends `RVVoiceFix.log`
and `UE4SS.log`.
