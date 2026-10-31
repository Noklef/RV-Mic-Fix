# Test procedure - v0.3.0-test

Local checks: `.\scripts\Build-Native.ps1 -Analyze -Test`. They do not prove
anything in the game.

After each step, send `RVVoiceFix.log` and `UE4SS.log`. Note: host or client,
number of players, the device chosen, and what each player heard.

## 1. Load and overlay (single player)

1. Install the pinned UE4SS and this mod. Do not run the old RVProximityFix
   mod.
2. Start the game. The log shows `Exact executable and loader hashes
   accepted`, `Loaded v0.3.0-test`, `Game-thread engine-tick callback is
   running`.
3. Press F7. A panel opens at the top left with a mouse cursor. The log shows
   `Overlay widgets built: N items.` If it names failed UMG calls, or the log
   shows `Overlay could not be shown`, send the log.
4. Click buttons (one action per click). Close with F7, Esc and Close. After
   each close, movement, camera and game menus work.
5. Fullscreen and windowed. Press F7 while a game menu is open: the cursor
   stays after close. Load a map with the overlay open: it closes and input
   returns.

## 2. Microphone (two players)

1. Join a session with another player. Close any game microphone test screen.
2. F7: "In use" names the device the game captures from.
3. Click another microphone. The row shows **IN USE - SAVED**. The other player
   hears that microphone (push-to-talk and open mic).
4. Restart and join again. The other player still hears the saved microphone
   (log: `Saved microphone re-apply` if the game opened another one).
5. Click "Windows default": no SAVED tag; the Windows default is used and is
   not changed.
6. Unplug the saved device: after three failures the mod stops and shows "pick
   it again".

## 2a. Sound menu label (single player)

1. F7: pick a device other than the Windows default.
2. Settings -> Sound: within 1 s, "Input Device" names the same device.
3. The "Voice Level" meter keeps the Windows default name (correct: it measures
   the game's preview capture).

## 2b. Sound menu button (single player)

1. Settings -> Sound: within 1 s, a left-aligned "RV Voice Fix" button appears
   above "Input Device". Log: `Settings -> Sound: RV Voice Fix button added.`
2. Click it: the overlay opens.
3. Close it with its Close button (Esc also closes the game menu). The Sound
   menu still takes mouse input.
4. Leave and reopen the menu: exactly one button.

## 2c. Settings "Voice Fix" tab (single player)

1. Open Settings: within 1 s, a "Voice Fix" tab appears after Keybinds. Log:
   `Settings: Voice Fix tab added.`
2. The page has headings Microphone, Players (only with remote players) and
   Troubleshooting. No gaps or cut-off rows; many players scroll.
3. Step the microphone: it switches about 1 s after the last step. Drag a
   volume slider and change Mute: F7 shows the same values. Write log: log
   shows `Snapshot written`.
4. Switch to Sound and back, then Back. The menu still works.
5. If the tab is missing or a control does nothing, send the log. F7 still
   works.

## 2d. MIC-DIAG (single player; step 4 needs two)

1. Start the game. The log shows `MIC-DIAG game '<name>' [<id>] | capture
   '<name>' ...`; a new line only on change. `game <...>` in angle brackets
   names what failed.
2. Settings -> Sound -> Input Device: pick another microphone. Within 2 s, does
   `capture` change to the same device as `game`?
3. Switch with the mod. Does `game` change too?
4. Two players, after step 2 only: which microphone does the other player hear?

## 3. Per-player volume (two or more players)

1. F7: each remote player shows a distance that changes as they move.
2. Set one player to 25%, then 200%, then Mute, while they speak. Other players
   do not change.
3. Restart and join again: the volume is kept.
4. A **GLOBAL** player may not respond until the repair restarts their voice.
   Report a **POSITIONAL** player that does not respond.

## 4. Proximity repair (host, one or more remote players)

1. Host. Set Auto-fix OFF (F7 or the Voice Fix tab).
2. A remote player speaks near, then far: **POSITIONAL**, audible falloff.
   Press F8.
3. Reset a checkpoint. Keep talking for **at least two minutes**: the fault is
   expected at a garbage collection, up to about a minute later. Watch for
   **GLOBAL** and a drop in the stale-talker count. Press F8 when the fault is
   audible.
4. Press F9. The player speaks again: **POSITIONAL** and audible falloff.
   Press F8.
5. Set Auto-fix ON. Repeat: three resets, map or menu changes, leave and
   rejoin, a reset during speech. Each repair shows a notice and is logged.
6. As a client, Fix now reports "runs only on the host".

`REGISTERED` and `RESTARTED` lines only show that a call returned. Audible
falloff is the proof.

## Automatic log lines

- `SESSION` and `PLAYER` snapshot: on a change of session, host role, players,
  talkers, or the stale/global counts (at most once per 30 s). `global=` above
  0 is the fault. F8 forces one.
- `RESTARTED <player> (<stream>)`: the repair stopped that voice.
- `VOLUME <player> <n>%: applied | no voice stream yet | FAILED`: 1 s after a
  slider stops, at once for Mute.
