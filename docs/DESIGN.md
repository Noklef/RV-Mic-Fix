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

## In-game overlay

UE4SS exports `Hook::RegisterProcessEventPostCallback`. The engine calls
`AHUD::ReceiveDrawHUD(SizeX, SizeY)` through `ProcessEvent` every frame from
`AHUD::DrawHUD`, with a valid canvas. In that callback the mod calls the HUD's
own reflected `DrawRect`, `DrawText` and `GetTextSize`. The overlay is drawn by
the engine into the game viewport, so it works in fullscreen.

- The callback compares the function pointer against one atomic value and
  returns. The pointer is non-null only while the overlay or a notice is
  visible. Every other `ProcessEvent` call (thousands per frame, some off the
  game thread) costs one atomic load and comparison.
- The `ReceiveDrawHUD` pointer is resolved from the live HUD instance
  (`GetFunctionByNameInChain`), so a Blueprint HUD override is matched too.
- If `HUD.bShowHUD` is false on open, the mod sets it true and restores it
  on close. If no HUD draw arrives within 2.5 s of opening, the mod closes the
  overlay and restores input, so game input is never left blocked. Both are
  logged.
- UI layout and hit-testing are pure C++ (`Overlay.cpp`) against a `Painter`
  interface. Native checks run them with a recording painter, without the game.

Input while open:

- `UWidgetBlueprintLibrary::SetInputMode_UIOnlyEx` makes the viewport ignore game
  input. `APlayerController::SetShowMouseCursor(true)` frees the cursor. Close
  restores game-only input and hides the cursor, unless the cursor was
  already visible when the overlay opened (a game menu was open). In that case it
  restores game-and-UI input and keeps the cursor.
- Mouse position comes from Win32 `GetCursorPos`, mapped from the game
  window's client rectangle to canvas pixels. Buttons are read with
  `GetAsyncKeyState`, only while the game window is in the foreground.
- F7 or Esc closes the overlay.

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

The remote player's voice synth is attached to the talker's
`ComponentToAttachTo`, which is owned by that player's pawn. Mapping synth to player
uses that attach parent. The maintenance pass sets the synth's audio component
`VolumeMultiplier` (`SetVolumeMultiplier`) to the chosen gain (0-200%, or mute)
when it differs. Settings are keyed by player display name in
`RVVoiceFix.ini`.

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
| `Main.cpp` | Entry point, hash gate, tick and ProcessEvent callbacks, command routing |
| `Reflection.*` | Reflection helpers: properties, structs, bools, parameter buffers, calls |
| `Voice.*` | Roster, talker/synth matching, repair, volume, per-player snapshot, F8 log |
| `Overlay.*` | Pure immediate-mode UI: layout, hit-testing, actions (unit tested) |
| `Hud.*` | `Painter` on AHUD canvas; input-mode and cursor control |
| `Prefs.*` | `RVVoiceFix.ini` load and save |
| `Devices.*` | Device enumeration, native capture switch, read-only capture query |

Rules kept from earlier versions: no UObject pointer is dereferenced after the
callback that obtained it. Pointer values may be kept only as identities for
comparison. Native capture access stays behind the executable hash gate.
