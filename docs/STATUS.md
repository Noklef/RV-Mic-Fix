# Status - 31 October 2026

Version 0.3.0-test, game CL-21945. Older record:
[handoff](history/HANDOFF-2026-10-29.md).

## Requirements

1. Fix proximity voice (host hears a player at any distance).
2. Per-player volume.
3. Switch the microphone without a restart.

All UI is in the game. No Windows windows.

## Local checks (build FAD1BE84)

`/W4 /WX /analyze` passes; 130 native checks pass (nine real devices); all 35
imports match the pinned `UE4SS.dll`.

## Verified in the game

- Hash gate, loading, tick callback.
- F7 overlay: renders, clicks, sliders, input and cursor restore.
- Native microphone switch: capture reopens on the chosen device; one reopen
  per click.
- Talker match and re-registration, also after a load (talkers live on the
  PlayerState).
- Volume and mute per player: multiplier 5.00 at 200% (game 2.50 = 100%).
  Steam ID keys.
- Settings -> Sound "Input Device" shows the device in use.
- Settings "Voice Fix" tab: headings, rows, survives menu changes.

## Not verified

- Build FAD1BE84 (deployed): MIC-DIAG without a crash, tab slider drag.
- Microphone switch on CL-21945, and what the other player hears after a
  switch.
- The voice restart on a GLOBAL voice; the GC hypothesis; an audible repair.
- Settings -> Sound button (TESTING step 2b).
- Whether `IsSubOption` indents the Mute row (a screenshot suggests it does).

## Known limits

- An unplugged saved microphone shows as "Windows default" in the tab.
- The tab appears up to 1 s after the menu opens (1 s poll). User decision:
  keep.
- Above 200% the engine gives no audible gain.

## Run log

| Run | Build / setup | Result |
| --- | --- | --- |
| 1 | v0.2.1, single | Mic switch works. Repair never ran: `VoiceSettings` lookup failed; now read from the talker's property. |
| 2 | HUD canvas | `ReceiveDrawHUD` never called (0 of 73,455 events). Moved to UMG. |
| 3 | UMG, single | Overlay works. Repeated reopens were repeated clicks (confirmed in run 4). |
| 4 | Host + 1 | Volume found no stream: the audible component is the synth's inner `AudioComponent`. Fixed. |
| 5-6 | Host + 1, load | After a load, two talkers on one PlayerState were ambiguous. Fixed: match by the PlayerState. |
| 7 | Host + 1; single | Volume 200% and mute applied after a load. Tab added. |
| 8 | B7480061 | Tab uses game rows. User asked for headings (done). |
| 9 | BC8DCDCB, host + 1 | CL-21945 loads; Steam IDs; 300% no louder than 200% (cap set back to 200). Tab stopped after "no tab to copy". Fixed in 8F1C3D48. |
| 10 | 8F1C3D48, host + 1 | Tab survives menu changes. Slider would not drag (step 5 on a 0-1 slider); MIC-DIAG `<no OnlineHelpers>`. Both fixed. |

## Game updates

| Update | Change | Pinned in |
| --- | --- | --- |
| CL-21905 (30 Oct) | ChangeDevice +0x520, globals +0xe7a0 | CE4934DB |
| CL-21945 (31 Oct) | ChangeDevice and vtable +0x450, manager +0x4000 | BC8DCDCB |

Details: [NATIVE-EVIDENCE.md](NATIVE-EVIDENCE.md).

## Review of 31 October 2026

[REVIEW-2026-10_31.md](REVIEW-2026-10_31.md). Fixed in FAD1BE84: MIC-DIAG
crashed the game about 1 s after load (the `FUniqueNetIdRepl` result had a null
vtable). **Do not run builds CD55BECE or 63687DEB.** Also one
`Prefs::StepPercent` constant. Other findings were rejected with reasons.

## Queue

1. The user tests build FAD1BE84 (next action).
2. MIC-DIAG result decides whether the microphone switch can use the game's
   reflected setter instead of addresses.
3. User decisions: keep or remove the Sound menu button; default auto-fix OFF;
   leave the PlayerState-root attach in the repair; commit.

## Next action

With a friend in the game, the user checks:

1. The game does not crash after load.
2. The tab's volume slider drags, and F7 shows the same percent.
3. Then sends `RVVoiceFix.log`.
