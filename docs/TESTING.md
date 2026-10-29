# Native test procedure

## Local checks

Run `scripts/Build-Native.ps1 -UE4SSDll <pinned DLL> -Test`. These checks create
the real Windows controls and enumerate actual capture devices. They exercise
Apply/Refresh routing, preservation of endpoint identity, queue order/bounds,
window lifecycle, repair timing/cancellation, the missing-base guard, and a
SHA-256 known-answer check. They never invoke the game's native capture calls.
`build/native-tests.exe --preview` displays the isolated UI for 45 seconds.
Use the copied `build/panel-preview.exe` for visual inspection while developing
so the test executable remains available for rebuilding.

## Loading and microphone selection: pending

1. Exit the game; install the pinned UE4SS build and this C++ mod. Do not run the
   old RVProximityFix Lua mod alongside it.
2. Launch windowed/borderless. Confirm `Exact executable and loader hashes
   accepted` and `Game-thread callback registered` in the mod's RVVoiceFix.log.
3. Press F7. Confirm the dropdown lists your Windows microphones and a default
   option. Verify F7/Esc and mouse close return to the game; check game input.
4. Join/host a session with another player. Close any built-in microphone test.
5. Select a second physical microphone, click Apply, and have the other player
   confirm sound comes from that microphone. Repeat in both directions. Test
   push-to-talk and open-mic modes without changing their game settings.
6. Change the Windows default manually, choose the default option, and Apply.
   Confirm transmission uses that endpoint. The mod must not set Windows defaults.
7. Test Refresh, a disconnected endpoint, duplicate names, joining/leaving a
   session, and a device connected after startup. Refused operations must give
   status and keep the previous capture usable. Do not force unsafe failures.
8. Repeat as a joining client. A native success result reports GUID/format/state
   checks; it does not establish that the selected capture feeds network audio.

## Proximity repair: requires two clients, pending

1. Host a session. Press F10 to disable automatic repair; confirm OFF in the log.
2. Have a remote player speak from several directions and distances. Record the
   working baseline and press F8.
3. Reset a checkpoint. Reproduce and record the global/non-directional defect
   before applying a repair. Press F8 again.
4. Press F9. Allow ten seconds after the current player roster settles. Have the
   remote player stop speaking and begin again. Compare direction and distance.
5. Press F8; retain both the mod log and UE4SS log. `REGISTERED` means only that
   ProcessEvent returned. Voices staying global is a failed repair result.
6. If manual repair works, enable auto with F10 and test three consecutive resets,
   travel/menu changes, player leave/rejoin, and reset during active speech.
7. Check a joining client: proximity repair must refuse mutation without local
   host authority. Microphone selection remains available.

Record game/loader hashes, host/client role, capture mode, chosen endpoint,
number of players, and audible results. Stop and restart if rollback is
incomplete or a native fault is logged. One player alone cannot establish that
remote positional voice has been repaired.

## Pending final UI

The user approved F7 only as a testing interface. After validating native
behavior, integrate the selector into SoundOptions and repeat input, focus,
capture-mode, apply/cancel, and reopening tests there.
