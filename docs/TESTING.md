# Test procedure - v0.3.0-test

## Local checks

Run `scripts/Build-Native.ps1 -Analyze -Test`. The checks run the pure panel
content: voice chips, volume and mute state, details, every button's action,
device buttons selecting the right endpoint, sliders starting at the saved
volume, the structure signature (text and value changes update in place;
details or a new player rebuild), the notice line, and slider snapping
(0-200%, 5% steps, invalid values). They also check settings
round trip (Unicode names, spaces, clamping, malformed lines, file replace), the
saved-microphone re-apply policy, the executable gate, a SHA-256 known answer,
and real Windows capture enumeration. They never call into the game.

## Step 1 - loading and overlay (single player is enough)

1. Exit the game. Install the pinned UE4SS build and this mod. Do not run the
   old RVProximityFix Lua mod alongside it.
2. Launch. Confirm in `RVVoiceFix.log`: `Exact executable and loader hashes
   accepted`, `Loaded v0.3.0-test`, `Game-thread engine-tick callback is
   running`.
3. Press F7 in the game world. Expect a dark panel at the top left and a free
   mouse cursor. The log shows `Overlay opened: UI-only input set, cursor
   shown` and `Overlay widgets built: N items.`
   - If that line names failed UMG calls, or the log shows `Overlay could not
     be shown`, the mod closes the overlay and restores input. Send the log.
4. Hover and click the buttons; each click acts once. Drag a slider (with
   another player present). Close with F7, Esc and the Close button. After
   each close, check that movement, camera and the game's own menus work.
5. Test fullscreen and windowed. Press F7 while a game menu is open; the cursor
   must stay visible after close. Leave the overlay open while the game loads
   a new map: it must close and give input back.

## Step 2 - microphone (two players)

1. Host or join a session with another player. Close any built-in microphone
   test screen.
2. Press F7. "In use" names the device the game captures from.
3. Click a second physical microphone. The status line reports the result and
   the row shows **IN USE - SAVED**. The other player confirms the sound comes
   from that microphone. Test push-to-talk and open-mic.
4. Restart the game and join again. The log shows `Saved microphone re-apply`
   if the game opened another device; the other player confirms.
5. Click "Windows default". Confirm the saved choice is cleared (no SAVED tag)
   and transmission uses the Windows default. The mod must not change Windows
   defaults.
6. Unplug the saved device. The mod must stop re-applying after three failures
   and show "pick it again".

## Step 3 - per-player volume (two or more players)

1. Press F7. Each remote player appears with a distance that changes as they
   move.
2. Drag one player's slider to 25%, then 200%; they speak each time. Mute them.
   Other players must be unaffected.
3. Restart the game and join again. The same player keeps the saved volume.
4. A player shown as **GLOBAL** may not respond to the slider until the repair
   restarts their voice. Report it if the slider has no effect on a
   **POSITIONAL** player.

## Step 4 - proximity repair (host with at least one remote player)

1. Host. Press F10 to turn auto-fix OFF (overlay button shows OFF).
2. Remote player speaks near, then far. Record the working baseline: chip
   **POSITIONAL**. Press F8.
3. Reset a checkpoint. Keep talking for **at least two minutes**; the
   hypothesis is that the fault appears at a garbage collection, up to about a
   minute after the reset. Watch for **GLOBAL** and for the stale-talker
   count dropping. Press F8 when the fault is audible.
4. Press F9 (Fix now). The remote player stops speaking and starts again.
   Confirm the chip returns to **POSITIONAL** and distance falloff is audible.
   Press F8.
5. Turn auto-fix ON (F10). Repeat three resets, travel/menu changes, player
   leave/rejoin, and a reset during speech. The overlay notice and log
   report each restore.
6. As a client, Fix now must report "runs only on the host".

Send `RVVoiceFix.log` and `UE4SS.log` after each step. Record host/client role,
capture mode, chosen device, number of players, and what each player heard.
`REGISTERED` and `Restarted` lines mean only that the call returned; audible
falloff is the only proof of repair.
