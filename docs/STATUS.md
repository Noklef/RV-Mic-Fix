# Continuation status - 29 October 2026 (Australia/Sydney)

The user authorized resuming HANDOFF.md. They want the final dropdown in
SoundOptions and explicitly approved an F7 window for testing.

## Implemented

- Draft sources compile/link, with new Main.cpp and Panel files.
- C++ entry point, exact game/loader hash gates, game-thread callback, retained
  callback lifetime, and file logging.
- F7 microphone dropdown with Apply, Refresh, status, and queued commands.
- F8 diagnostics, F9 bounded repair burst, F10 automatic-repair toggle.
- World/player polling schedules three delayed host repair attempts.
- Capture name/GUID preflight, format/state checks, switch/rollback verification;
  proximity property-write rollback attempt.
- Native build/package/install helpers; old Lua runtime/tests removed.

## Local verification

- MSVC x64 Release `/MD` build/link against pinned UE4SS exports.
- `start_mod` and `uninstall_mod` exports confirmed with dumpbin.
- 19 native checks pass; Windows enumeration returned nine capture devices.
- Independent review completed; its object-flags documentation finding was
  addressed with named flags checked against the loader reference.
- ZIP contents checked: native DLL/manifest/enablement plus documentation and
  license, with no Lua runtime or UE4SS loader. Isolated installation and
  overwrite refusal passed; the fixture was removed.
- Visual preview remains unverified: Computer Use rejected launching
  panel-preview with "was not approved to use panel-preview".

## Remaining queue

1. Load the exact game/loader; verify reflection, lifecycle, and input.
2. Prove capture ownership and test live switching, push-to-talk/open-mic
   preservation, recovery, and session transitions.
3. Reproduce the reset defect with two clients; measure direction/distance
   before reset, after failure, and after manual/automatic repair.
4. Integrate the selector into SoundOptions after the test route is validated.

The game installation has not been changed. No game runtime result or audible
repair is claimed. See TESTING.md and NATIVE-EVIDENCE.md for the next checks.
The user will run game tests and report back. A single-player launch can check
loading/F7; this test build has no new SoundOptions control.

Workflow improvement: compile each native feature in this repository and record
the executable result before adding the next feature. Use the separate
panel-preview executable during visual checks to avoid locking native-tests.
