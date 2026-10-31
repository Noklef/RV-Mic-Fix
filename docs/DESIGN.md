# Design - v0.3.0-test

| Goal | Approach |
| --- | --- |
| 1. Proximity fault | Re-register talkers continuously; restart voices that play without attenuation |
| 2. Per-player volume | Scale each remote voice's `VolumeMultiplier`; save by Steam ID |
| 3. Microphone switch | Native `ChangeDevice` ([NATIVE-EVIDENCE.md](NATIVE-EVIDENCE.md)); saved and re-applied |

All UI is in the game. There is no Windows window.

## Rules

- All work runs in the engine post-tick callback, under one mutex taken with
  `try_lock`, so the game never waits.
- A UObject pointer is used only in the callback that found it. Between ticks
  the mod keeps `FWeakObjectPtr`s, or pointer values as keys that are never
  dereferenced. UI actions carry values (device GUID and name, player key).
- Native capture access stays behind the executable hash gate.

## Goal 1 - proximity fault

**Hypothesis (not observed):**

1. `UVOIPStatics` maps each player ID to one `UVOIPTalker`.
2. A checkpoint reset respawns pawns. A new talker registers the same ID.
3. At the next garbage collection (about 61 s), the old talker's `BeginDestroy`
   clears that map entry, which now belongs to the new talker.
4. The next voice from that player finds no talker, gets default settings (no
   attenuation, no attachment) and plays as 2D audio.

This explains "sometimes" (GC timing), and why a short repair burst after a
reset cannot work.

**Repair** (auto-fix, default ON, host only; a client never changes talkers or
voices, but volume works for everyone):

- Every 2 s, match each remote player to their current talker. Talkers marked
  as garbage are skipped. A talker on the current pawn ranks above one matched
  only through the PlayerState.
- Restore a stale `Settings.ComponentToAttachTo` and a missing `SA_VOIP`
  attenuation, then call `RegisterWithPlayerState`. Registration only
  overwrites the map entry, so repeats are harmless.
- Voice settings apply only when playback starts. A voice playing without
  attenuation is stopped once (rate-limited per voice); the next packet
  restarts it with the repaired settings.

Evidence to collect: a GLOBAL voice tens of seconds after a reset, just after
the stale-talker count drops.

## Goal 2 - per-player volume

Observed in the game: the `VoipListenerSynthComponent` has no attach parent.
Its inner `AudioComponent` is attached to the remote PlayerState's
`DefaultSceneRoot` and carries the audible attenuation. Stream-to-player
matching and the GLOBAL check use this inner component.

The game sets `VolumeMultiplier` to 2.5. The mod reads it once per component as
100% and sets `base x gain` (0-200%). Above 200% the engine gives no audible
gain.

Keys: Steam ID (`PlayerState.UniqueID` through
`OnlineHelpers.Conv_FUniqueNetIdReplToString`, 5-20 digits), else the display
name. A
name-only entry moves to the Steam ID when that player is first seen. Known
limit: an ID-less player with the same name then loses it (non-Steam sessions
only).

A GLOBAL voice has no attach parent, so it cannot be matched to a player until
the repair restarts it.

## Goal 3 - microphone

Click a device: the switch is queued for the game thread and saved by name. If
the game reopens capture on another device (for example after the Sound menu),
the mod re-applies the saved one: rate-limited, and stopped after three
failures until the user picks again. "Windows default" is never re-applied.

The Settings -> Sound "Input Device" text is the Windows default device, not a
game setting (Redpoint voice chat is inactive). While that menu is open, the
mod writes the device in use into it. The "Voice Level" meter belongs to the
game's preview capture and always uses the Windows default; the mod leaves it.

`MIC-DIAG` log lines compare the game's Redpoint input device with the native
capture. They decide whether the game's reflected setter can replace the
address-based switch.

## Overlay (F7)

A HUD canvas does not work: the game's plain `HUD` never dispatches
`ReceiveDrawHUD`. The overlay is a UMG tree built through reflection:

1. `UWidgetBlueprintLibrary::Create` makes a plain `UUserWidget`. The mod
   clears `CLASS_Abstract` for that one call.
2. `UGameplayStatics::SpawnObject` creates the native widgets in its
   `WidgetTree`; children are added with the reflected `AddChild*` functions.
3. Text is made with `Conv_StringToText`; the mod releases its copy with
   `FProperty::DestroyValue` after `SetText`.
4. `AddToViewport(1000)` shows it. Slate keeps it alive while it is shown.

`Overlay.cpp` turns state into rows (pure C++, unit tested). If the row
structure is unchanged, widgets update in place; otherwise they are rebuilt.
Buttons are polled each tick (`IsPressed`, `IsHovered`); sliders with
`GetValue`. No delegates.

While open: UI-only input and a visible cursor. Close restores the previous
input mode. F7 or Esc closes it (keys are read only while the game has focus).
If the widget leaves the viewport (level travel), the overlay closes and
restores input.

## Settings tab

`MenuTab` registers a "Voice Fix" tab in the game's options menu by copying an
existing tab. Its page is a scroll box with one game heading
(`WG_HeadlineDivider`) and one copy of the Gameplay `RGListView` per section:
Microphone, Players (only with remote players), Troubleshooting. Each list sits
in a size box set to the measured row height, because a list in an auto-sized
slot shows one row.

Rows are the game's own option items (`OptionListItemBar_C` and others).
Values are read back from the items. The slider entry runs 0-1, so its step is
`StepPercent / MaxPercent`. A microphone step applies 0.8 s after the last
press. The tab is checked every 1 s, so it can appear up to 1 s after the menu
opens.

## Modules

| File | Responsibility |
| --- | --- |
| `Main.cpp` | Entry point, hash gate, tick callback, command routing, MIC-DIAG |
| `Common.*` | Logging, hashing, shared helpers |
| `Reflection.*` | Properties, structs, parameter buffers, calls |
| `Voice.*` | Roster, talker and voice matching, repair, volume, snapshots |
| `Model.hpp` | Plain display values (no Unreal pointers) |
| `Overlay.*` | Rows, structure signature, slider snapping (unit tested) |
| `Widgets.*` | Overlay widgets, Sound menu button, Settings tab |
| `Input.*` | Player controller, input mode, cursor |
| `Prefs.*` | `RVVoiceFix.ini` load and save |
| `Devices.*` | Device list, native capture switch and query |
