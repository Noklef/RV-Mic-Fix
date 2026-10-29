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

## Implemented in 0.3.0-test

- In-game overlay drawn on the HUD canvas from the `ReceiveDrawHUD`
  ProcessEvent post-callback; UI-only input and cursor while open; automatic
  close if no draw arrives.
- Microphone list with the device in use, click to switch, saved choice,
  automatic re-apply (rate-limited, stops after three failures).
- Per-player list: distance, POSITIONAL/GLOBAL state, 0-200% slider, mute,
  saved in `RVVoiceFix.ini`.
- Proximity repair every 2 s on the host: garbage talkers excluded, settings
  restored, re-registration, bounded restart of voices playing without
  attenuation. Struct members are read through `FStructProperty::GetStruct`,
  which fixes the v0.2.1 failure.
- Notices for repair events while the overlay is closed; F8 snapshot.

## Local verification

- MSVC x64 `/W4 /WX /analyze` build passes. 123 native checks pass; nine real
  capture devices enumerated.
- All 33 imports were checked against the installed pinned `UE4SS.dll`
  (`-UE4SSDll`).

## Not verified (needs the game)

- `ReceiveDrawHUD` is observed and the HUD draw functions work in this game.
- Input mode and cursor restore correctly after close.
- Reflected names resolve: `CachedPlayerState`, `AttachParent`,
  `AudioComponent`, `VolumeMultiplier`, `PlayerNamePrivate`,
  `SetShowMouseCursor`, `GetDistanceTo`, `VoipListenerSynthComponent`.
- The GC hypothesis for the proximity fault, and an audible repair.
- Transmitted audio after a microphone switch.

## Next action

The user runs [TESTING.md](TESTING.md) steps 1-4 and sends `RVVoiceFix.log`
and `UE4SS.log`.
