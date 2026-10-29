# Design - v0.3.0-test

Written 29 October 2026, after the first in-game run of v0.2.1-test. This is the
working plan for the three user-facing goals. Statements marked **hypothesis**
are not yet confirmed by a live multiplayer test.

| Goal | Approach |
| --- | --- |
| 1. Host hears one player at full volume regardless of distance | Continuous talker re-registration plus restart of voices that are playing without attenuation |
| 2. Per-player volume | Volume multiplier on each remote player's voice playback component, saved by player name |
| 3. Change microphone without restarting | Existing native `ChangeDevice` route, now chosen in-game, saved, and re-applied if the game recreates capture |

All controls and diagnostics are drawn **inside the game**. There is no Windows
window. The v0.2.1 F7 Win32 panel was removed.

## Evidence from the first in-game run (v0.2.1-test)

Source: `ue4ss/Mods/RVVoiceFix/RVVoiceFix.log` and `ue4ss/UE4SS.log`, single
player, 29 October 2026.

- Hash gate, `on_unreal_init`, engine-tick callback, and F7 all worked.
- Four `Capture reopened` results from the native microphone switch. No fault,
  no rollback. Audible transmission is still unconfirmed.
- **Every proximity attempt stopped at `VoiceSettings reflection is
  unavailable`.** `FindAllOf(L"ScriptStruct")` did not return
  `/Script/Engine.VoiceSettings`. The repair code never reached a talker in any
  build. v0.3 reads the struct type from the talker's own `Settings` property
  (`FStructProperty::GetStruct`), so it does not depend on a global search.

## In-game overlay (UMG)

**Tried first and failed:** drawing on the HUD canvas from
`AHUD::ReceiveDrawHUD`. The v0.3.0 diagnostic build counted 73,455
`ProcessEvent` calls in one second and **zero** `ReceiveDrawHUD` calls. The game
uses the plain engine `HUD` class, and that event is never dispatched. The
ProcessEvent hook was removed.

The overlay is now a UMG widget tree, created through reflection on the game
thread:

1. `UWidgetBlueprintLibrary::Create` makes a plain `UUserWidget` owned by the
   local player controller. `UUserWidget` is flagged abstract only to keep
   designers from placing the base class. The mod clears `CLASS_Abstract` for
   that one call and then restores it. A native instance gets an empty
   `WidgetTree`.
2. `UGameplayStatics::SpawnObject` creates native widgets (`CanvasPanel`,
   `Border`, `VerticalBox`, `HorizontalBox`, `SizeBox`, `TextBlock`, `Button`,
   `Slider`) with the widget tree as outer. The canvas becomes
   `WidgetTree.RootWidget`; children are added with the reflected `AddChild*`
   functions.
3. Text is `FText` from `KismetTextLibrary::Conv_StringToText`. `SetText` copies
   it; the mod then releases its own copy with `FProperty::DestroyValue`.
   Font size is written to `TextBlock.Font.Size` before Slate builds the widget.
4. `AddToViewport(1000)` shows it. While it is in the viewport, Slate keeps the
   user widget and its tree alive. The mod holds only `FWeakObjectPtr`s.

Content is pure C++ (`Overlay.cpp`): `describe()` turns the current state into
rows of items, and native checks test it without the game. `shape()` keeps only
the structure (kinds, widths, actions). When only text, colours or values
change, the widgets update in place. A different shape (player joined, details
toggled, device list changed) rebuilds them. Each tick, the mod reads every
button's `IsPressed`/`IsHovered`. A release over the button is a click. Sliders
are read with `GetValue`. Nothing is bound to delegates.

If the widget is gone or no longer in the viewport (for example after level
travel), the overlay closes and restores input. Notices use the same builder
with one line of text, `HitTestInvisible`, and no input change.

Input while open:

- `UWidgetBlueprintLibrary::SetInputMode_UIOnlyEx` makes the viewport ignore game
  input. `APlayerController::SetShowMouseCursor(true)` frees the cursor. Close
  restores game-only input and hides the cursor, unless the cursor was
  already visible when the overlay opened (a game menu was open). In that case it
  restores game-and-UI input and keeps the cursor. Confirmed in game.
- F7 or Esc closes the overlay. Keys are read with `GetAsyncKeyState`, only
  while the game is in the foreground.

## Goal 1 - proximity fault: hypothesis and repair

**Hypothesis (consistent with the report, not yet observed):**

1. The engine's VOIP map is static: `UVOIPStatics` maps each player's unique
   net ID to one `UVOIPTalker`.
2. A checkpoint reset destroys and respawns pawns. The pawn's `BP_VOIPTalker`
   is marked as garbage, and a new talker registers the same player ID.
3. The old talker's `BeginDestroy` runs at the next garbage collection (UE
   default interval about 61 seconds). It resets the map entry for its player
   ID. That entry now belongs to the *new* talker.
4. When that player next starts speaking, the voice engine finds no talker. It
   applies default `FVoiceSettings`, with no attenuation and no attachment, and
   the voice plays as 2D audio at full volume everywhere.

This explains "sometimes" (it depends on GC timing) and why a repair burst at 3,
6.5 and 10 seconds cannot work: the map is broken up to a minute later.

**Repair (auto-fix, default ON, host only):** a client never changes talkers or
voice streams. Volume control works for host and clients.

- Every 2 seconds while remote players exist, match each remote player to their
  current talker. Talkers whose object or owner is flagged as garbage are
  excluded. A talker owned by the current pawn ranks above one matched only
  through the PlayerState.
- If the talker's `Settings.ComponentToAttachTo` is stale, restore it. If its
  attenuation is missing, restore `SA_VOIP`. Then call `RegisterWithPlayerState`.
  Registration only overwrites the map entry, so repeating it is harmless.
- Voice settings apply only when playback *starts*. A remote voice synth
  (`VoipListenerSynthComponent`) that is playing without attenuation is
  therefore stopped once (rate-limited per synth). The voice engine restarts it
  on the next packet with the repaired settings.
- The overlay shows each player as **positional** or **GLOBAL** and logs
  each repair.

Confirming evidence to collect: a `GLOBAL` state that appears tens of seconds
after a reset, or the stale-talker count dropping to zero (GC) right before
it.

## Goal 2 - per-player volume

In this game the `VoipListenerSynthComponent` itself has no attach parent. Its
inner `AudioComponent` is attached to the talker's `ComponentToAttachTo`, the
remote PlayerState's `DefaultSceneRoot`, and carries the attenuation that is heard
(observed in game, sixth run). Mapping stream to player (parent owner = pawn or
PlayerState, or parent = talker attach) and the GLOBAL check
use that inner component. The game sets its `VolumeMultiplier` to 2.5; the mod
reads that value once per component before any change and treats it as 100%,
so the maintenance pass sets `base x gain` (0-200%, or mute) when it differs.
Settings are keyed by Steam ID (`PlayerState.UniqueID` through
`OnlineHelpers.Conv_FUniqueNetIdReplToString`, 5-20 digits), else by display
name, in `RVVoiceFix.ini`; a name-only entry moves to the ID on first sight.
Known limit: the move takes any entry with that name, so an ID-less player with
the same name loses it. Steam sessions give every player an ID, so this needs a
non-Steam session.

A voice that is playing **GLOBAL** has no attach parent, so it cannot be mapped
to a player until the repair restarts it. Volume control therefore depends on
goal 1 for that player.

## Goal 3 - microphone

The native switch route (`docs/NATIVE-EVIDENCE.md`) is unchanged. The overlay
lists Windows capture devices and marks the one the game's capture is using now.
Clicking one queues the switch for the game thread. The choice is saved by
device name. When the game recreates or reopens capture (for example after
SoundOptions), and the active device no longer matches a saved named device,
the mod re-applies it. Re-apply is rate-limited, and it stops after three
consecutive failures until the user picks again. "Windows default" is never
re-applied automatically.

## Module layout

| File | Responsibility |
| --- | --- |
| `Main.cpp` | Entry point, hash gate, engine-tick callback, command routing |
| `Reflection.*` | Reflection helpers: properties, structs, bools, parameter buffers, calls |
| `Voice.*` | Roster, talker/synth matching, repair, volume, per-player snapshot, F8 log |
| `Overlay.*` | Pure panel content: rows, structure signature, slider snapping (unit tested) |
| `Widgets.*` | UMG widget tree from panel rows; button and slider polling |
| `Input.*` | Local player controller; input mode and cursor |
| `Prefs.*` | `RVVoiceFix.ini` load and save |
| `Devices.*` | Device enumeration, native capture switch, read-only capture query |

Rules kept from earlier versions: no UObject pointer is dereferenced after the
callback that obtained it. Pointer values may be kept only as identities for
comparison. Native capture access stays behind the executable hash gate.
