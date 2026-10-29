# RV Voice Fix — handoff

> **Continuation update, 29 October 2026:** The user authorized resuming this
> handoff. A compiled C++ test build, F7 test window, entry point, game-thread
> controls, native checks, and build/package/install helpers now exist. The old
> Lua runtime has been removed. See [current status](docs/STATUS.md) and
> [README](README.md). The remainder is the historical handoff; its implementation
> status statements are superseded. Live game validation and final SoundOptions
> integration remain pending.

Updated: 29 October 2026 (Australia/Sydney).

## Read this first

**There is no compiled or verified C++ mod.** Seven C++ source files have been copied into this repository's `src/` directory. There is no native entry point, completed microphone dropdown, build script, import library, or DLL. The previous Lua prototype remains in the repository. Neither implementation has been tested in the live game.

The user asked to stop implementation and provide this handoff. Do not interpret this document as permission to resume work. No game installation was modified during this work.

## User's intended result

Create a single voice-fix mod for **RV There Yet?** in:

`C:\git\Noklef\RV-Mod-Fix`

Requested features:

1. Repair proximity/directional voice that works initially and becomes non-directional/global for the host after a checkpoint reset.
2. Add an in-game microphone dropdown so players can select another input without quitting and reopening the game.

The user explicitly requested one suitable implementation language, with no Lua/C++ mixing. The latest implementation decision was an entirely **C++ UE4SS mod** for both features. PowerShell build/package helpers are infrastructure; there should be no Lua runtime bridge.

The user ruled out Voicemeeter as the cause. Do not restart that investigation. The user confirmed that voice worked before the reset and failed after it.

Keep the implementation small. Avoid speculative architecture, excessive mocked tests, and prolonged research without a concrete result in the requested repository. Be direct about unfinished work. Prior communication implied progress too far ahead of actual delivery.

## Repository state at handoff

Last observed `git status --short`, before creating this document:

```text
?? LICENSE
?? src/
```

Last previously observed commit: `ae2c1b7 Initial mod WIP fix`.

`LICENSE` was already untracked; preserve it. No commit or PR was created. No tracked Lua files were removed or changed in this turn.

### New C++ files: present, uncompiled

| File | Contents |
| --- | --- |
| `src/Common.hpp` | Executable/loader hashes, memory-read helpers, capture-state policy, a small delayed roster-change scheduler. |
| `src/Common.cpp` | Windows BCrypt SHA-256, module paths, GUID conversion, memory-region checks, UTF-8 file logging. |
| `src/Devices.hpp` | Device enumeration and native capture-switch interfaces. |
| `src/Devices.cpp` | DirectSound capture-device enumeration, fresh Windows default-input lookup, native capture lookup, switch preflight and rollback attempt. |
| `src/Proximity.hpp` | Roster signature and repair interfaces. |
| `src/Proximity.cpp` | Reflection-based current-world/player/talker lookup, host checks, attachment/attenuation repair and talker registration. |
| `src/Ue4ssAbi.hpp` | Narrow, handwritten, release-pinned declarations of exported UE4SS C++ APIs and the loader base-class ABI. |

**These files are draft code, not a working installation.** They have not been compiled, linked, loaded, or exercised. There may be compile errors, ABI mistakes, incorrect reflected names, or incorrect native assumptions.

The hash constants exist, but no entry point currently enforces them. `CaptureControl` accepts a base address supplied by its future caller; do not invoke it without the executable hash gate.

### Existing Lua prototype

```text
Mods/RVProximityFix/enabled.txt
Mods/RVProximityFix/Scripts/main.lua
Mods/RVProximityFix/Scripts/config.lua
Mods/RVProximityFix/Scripts/controller.lua
Mods/RVProximityFix/Scripts/engine.lua
Mods/RVProximityFix/Scripts/repair.lua
tests/run.lua
tests/run.cjs
package.json
package-lock.json
scripts/Build-Package.ps1
scripts/Install-Mod.ps1
README.md
docs/TESTING.md
docs/INVESTIGATION.md
```

The README, installer, packaging, and tests still describe/use the **old Lua prototype**, not the new C++ source. Do not package that tree and call it the native voice-fix mod.

The old prototype had 37 previously passing mocked Lua tests. That does not validate actual game behavior or the new C++ code. No new C++ tests exist.

The old prototype attempted to:

- Resolve the current viewport world and current GameState player roster.
- Confirm a local listen-server host with authority.
- Require two-way current Pawn/PlayerState ownership.
- Match existing remote native VOIPTalkers through cached state, owner, outer, or attachment owner.
- Skip local, ambiguous, duplicate, or retired talkers.
- Restore an attachment to the current pawn's mesh/root and restore missing game attenuation.
- Call `RegisterWithPlayerState` on the existing talker.
- Leave valid attenuation/source-effect choices intact.

Its repair trigger used lifecycle hooks and delayed retries. Its keys were F8 diagnostics, F9 manual repair, F10 automatic-repair toggle. These controls are not wired in the C++ draft.

## Local game and supplied log

Game installation:

```text
D:\Games\Steam\steamapps\common\Ride
D:\Games\Steam\steamapps\common\Ride\Ride\Binaries\Win64\Ride-Win64-Shipping.exe
```

Observed executable size: `181991424` bytes.

SHA-256:

```text
12b24146b30d550fe47a4179c7b05d1f21b69f058524e0bba4b41397e3e66221
```

Observed version: **1.3.0.20702**, Unreal **5.6**.

Version string: `Ride-CL-20702-6ab26b87853e185d15f6f574`.

UE4SS was not installed into the game directory when inspected. The game was not running at the last game-process check; recheck before any future installation.

Original user-provided log:

```text
C:\Users\Tim\.codex\attachments\decbafdf-9118-4532-bd7d-d443c9f999f5\Pasted text.txt
```

Approximately 915 KB / 6,817 lines. It describes a Steam listen-server session with crossplay disabled. Remote voice registration and decoding occurred; this is not evidence that spatial playback remained correct.

Checkpoint reloads were observed around UTC `10:54:27` and `11:26:23`, corresponding to local log times `21:54:27` and `22:26:23` on the session date.

Entering SoundOptions around UTC `10:53:44`, `10:53:52`, and `10:54:04` opened WASAPI 44.1 kHz float32 capture. Applying settings stopped/started network voice. That does **not** establish that it reopened the actual network microphone capture. A separate settings-meter capture is an inference that needs runtime confirmation.

Game manifest:

```text
D:\Games\Steam\steamapps\common\Ride\Manifest_UFSFiles_Win64.txt
```

Names observed: `BP_VOIPTalker` (around line 5191), `SA_VOIP` (6510), `SC_VoiceChat` (6618). These are packaging evidence; the Blueprint logic was not decompiled.

Attenuation path used by the prototypes:

```text
/Game/Ride/Audio/Attenuation/SA_VOIP.SA_VOIP
```

## What the investigation supports — and what it does not

### Proximity repair

The reset-related stale talker/attachment/registration explanation remains a **hypothesis**. The log and user report motivate it; no live inspection or audible test confirmed it.

Re-registering a talker may not repair an already active remote playback stream. No implementation currently forces a verified remote playback restart. Entirely missing talkers or unidentifiable transient talkers remain unsupported. If registration completes but distance/direction remain broken, the repair has not succeeded.

### Microphone switching

Focused static analysis found a native Windows voice-capture `ChangeDevice` route in the installed executable. A full game restart is not intrinsically required by that native API.

That finding does **not** establish that the draft code safely targets the network capture in every runtime state. The future implementation must identify the exact active capture, preserve the capture mode/format, verify success, and handle failures.

The engine caches capture devices. An empty device name uses its cached default record. To honor the *current* Windows default, resolve the current endpoint and pass an explicit matching engine device name. A microphone connected after game startup may be absent from the cached map; the draft refuses that switch instead of reinitializing the whole manager.

Do not change Windows system-wide audio defaults to implement the dropdown.

## Native capture findings for this exact executable

These addresses came from focused PE/xref and Ghidra analysis. Function names are inferred from behavior, diagnostic strings and vtable positions because symbols are stripped. Reconfirm call signatures and return conventions before treating the draft calls as safe.

Preferred PE image base: `0x140000000`. Runtime ASLR requires **module base + RVA**.

| Item | Preferred VA | RVA |
| --- | --- | --- |
| Capture-manager global pointer | `0x14a6c7210` | `0xa6c7210` |
| Windows capture vtable | `0x148c1fd18` | `0x8c1fd18` |
| ChangeDevice | `0x144e352e0` | `0x4e352e0` |
| CreateCaptureBuffer | `0x144e35320` | `0x4e35320` |
| Device-name hash helper | `0x141097ca0` | `0x1097ca0` |
| Device map lookup | `0x143077020` | `0x3077020` |
| Manager initialization | `0x144e34280` | `0x4e34280` |
| Manager Init/enumeration candidate | `0x144e37020` | `0x4e37020` |
| Capture constructor | `0x144e34710` | `0x4e34710` |
| Capture destruction/removal | `0x144e34a30` | `0x4e34a30` |
| Capture factory/cold function | `0x144e33d73` | `0x4e33d73` |

Capture vtable slots observed:

```text
0 destructor     0x144e34c50
1 Init           0x144e373e0
2 Shutdown       0x144e39160
3 Start          0x144e39190
4 Stop           0x144e39220
5 ChangeDevice   0x144e352e0
```

Observed layouts:

- Capture manager begins with a `TArray<void*>` of live capture instances: data pointer at `+0`, int32 count at `+8`, int32 capacity at `+12`.
- Capture allocation size observed: `0x198` bytes.
- Capture internal-data pointer: `capture + 0xa0`.
- Capture state: int at `capture + 0xb0`.
- Internal-data GUID: `internal + 0`.
- Native capture device: `internal + 0x10`.
- Native capture buffer: `internal + 0x28`.
- Wave format: `internal + 0x30`; uint16 channels at `+0x32`, uint32 sample rate at `+0x34`, uint16 bits/sample at `+0x3e`.

Capture-state interpretation, partly inferred from code/standard engine enum:

```text
0 uninitialized
1 not capturing
2 OK / active
3 no data / active (Start sets this)
4 stopping (Stop sets this)
```

ChangeDevice rejects uninitialized capture. CreateCaptureBuffer stops/releases the existing buffer, validates format, looks up the name/default record, and creates the replacement. Therefore **preflight the selected name before calling it**; failure after teardown can otherwise leave no usable capture.

Manager device map starts at `manager + 0x58`. Lookup takes the map, a hash, and an FString-like name and returns an index or -1. The map value/device record was found at:

```text
*(manager + 0x58) + index * 0x40 + 0x10
```

The record begins with a 16-byte FString followed by a 16-byte GUID. The cached default record is at `manager + 0x30`.

FString call argument layout used in the draft: pointer + int32 Num + int32 Max, including the trailing NUL in Num/Max. The name-hash helper is called with length excluding NUL.

Device selection draft behavior:

1. Enumerate DirectSound capture devices using ANSI descriptions converted through CP_ACP, matching the game's observed naming route.
2. Omit the duplicate null-GUID default alias.
3. Resolve a fresh system-default input through `IMMDeviceEnumerator`, `eCapture/eConsole`, and `PKEY_AudioEndpoint_GUID`.
4. Read the manager's live capture list and require exactly one initialized object with the exact capture vtable.
5. Refuse ambiguous multiple captures, unsupported states/formats, disconnected devices, duplicate names, or names absent from the cached engine map.
6. Save the old GUID/name, format, and capture state.
7. Call ChangeDevice; call Start only if capture was active.
8. Attempt rollback if switching fails; report rollback failure honestly.

Closing the game's microphone-test/settings screen may matter when multiple captures exist. This is not yet runtime-tested.

Other native wrappers found but not proven to control this session's Steam capture:

```text
SetVoiceChatAudioInputDevice UFunction exec: 0x145473fa0
Forwarded native implementation:          0x145490240
Redpoint SetInputDeviceId:                 0x145334060
GetDefaultCaptureDevice wrapper:          0x1456de250 -> 0x1457183a0
InitVoiceCaptureComponent wrapper:        0x1456df310 -> 0x14571d830
```

One inspected voice-engine console-command handler at `0x144e895b0` handled codec options/dumps, not a microphone selector. That is a bounded observation, not proof no other console command exists.

## UE4SS dependency and ABI caveats

Downloaded official experimental release:

```text
UE4SS_v3.0.1-1183-gcdbc0b66.zip
zDEV-UE4SS_v3.0.1-1183-gcdbc0b66.zip
```

Release tag was `experimental-latest`; it moves. Pin the exact asset/build, not the tag's future contents.

Downloaded development archive:

```text
C:\Users\Tim\Documents\Codex\2026-10-29\fge\work\ue4ss-dev.zip
```

Extracted DLL:

```text
C:\Users\Tim\Documents\Codex\2026-10-29\fge\work\ue4ss-dev\ue4ss\UE4SS.dll
```

DLL size: `20641280` bytes. SHA-256:

```text
c273e7228687b647226fd63d7ca65888d313ece6be18996ec1320d00a5efe1f8
```

The zDEV archive supplied DLL/PDB/docs but no usable C++ header/import-library bundle. The upstream Unreal dependency (`Re-UE4SS/UEPseudo`) was unavailable through the attempted public repository route. Do not spend another long investigation repeating that.

`src/Ue4ssAbi.hpp` is a provisional workaround: it redeclares a narrow exported ABI against this precise DLL. This is fragile and needs compilation and runtime validation. Prefer an available supported SDK if that is straightforward; do not assume a handcrafted header is universally compatible.

The base-class field/vtable order was copied from the public pinned `CppUserModBase.hpp` at commit `cdbc0b66`. Its expected x64 release size is 192 bytes: vptr, vector of shared GUI-tab pointers, five std::wstring metadata fields. Its virtual ABI includes Lua lifecycle methods; those are loader compatibility declarations only and do not implement a Lua mod.

The official header warns that mod and UE4SS must use matching C runtime/configuration. Intended toolchain: MSVC x64 Release, `/MD`, C++20, iterator debug level 0. Successful compilation alone does not prove complete ABI compatibility.

Relevant exported APIs found in the DLL:

- `UObjectGlobals::FindAllOf(const wchar_t*, std::vector<UObject*>&)`.
- `UObject::GetFullName(UObject*) const`, `GetWorld() const`.
- `UObject::GetPropertyByNameInChain`, `GetValuePtrByPropertyNameInChain`.
- `UObject::GetFunctionByNameInChain`, `ProcessEvent`.
- `UObjectBase::GetObjectFlags`, `GetOuterPrivate`.
- `UStruct::GetChildProperties`, `GetPropertiesSize`.
- `FField::GetName`, exported private `GetNext` / `GetClassPrivate` accessors.
- `FProperty::GetOffset_Internal`, `GetSize`.
- `FObjectPropertyBase::GetObjectPropertyValue`, `SetObjectPropertyValue`.
- `FWeakObjectPtr::Get() const`, assignment from `UObject*`.
- `UFunction::GetParmsSize`, `GetReturnValueOffset`.
- Legacy `Hook::RegisterEngineTickPostCallback(std::function<void(UEngine*, float)>)`, returning void.

Unreal work must run on the **game thread**. `CppUserModBase::on_update` is not the correct place for UObject/native capture operations. Proposed route was the engine-tick callback. No callback or entry point has yet been implemented.

The legacy callback has no unregister handle. A DLL-lifetime strategy is needed before unloading; the tentative idea was pinning the DLL, a noncapturing callback, and a cleared active-instance pointer on mod destruction. That is a proposal, not existing code.

## Unfinished implementation and review points

There is currently no `Main.cpp`, no UE4SS `start_mod`/`uninstall_mod` exports, and no native package.

If the user later authorizes continuing, the remaining implementation is:

1. Compile the current sources promptly and resolve actual errors before more optional research.
2. Add the smallest viable native entry point and game-thread callback, enforcing exact game/loader compatibility before native access.
3. Implement the microphone dropdown and Apply/Refresh/status controls in C++.
4. Wire manual diagnostics/repair and a bounded post-reset repair trigger.
5. Add native build/package/install instructions; replace the Lua runtime only once the C++ replacement is concrete.
6. Build a DLL, verify imports/exports, and perform focused tests.
7. Document and perform live microphone and multiplayer tests, with the user's help where required.

A **F7 game-owned Win32 panel** was being considered as a minimal dropdown UI. It is not implemented, not integrated into SoundOptions, and not a user-confirmed UI requirement. Do not claim an existing native settings-menu integration. Exclusive fullscreen visibility and game/UI input handling would need consideration.

Current draft concerns that deserve review rather than assumptions:

- Native function calling conventions, bool/return types, RVAs, and field meanings have not been verified by an actual invocation.
- The handcrafted UE4SS ABI header needs exact mangled import matches and matching runtime library configuration.
- The reflection route assumes modern FField accessors, specific field kinds, and `ScriptStruct /Script/Engine.VoiceSettings` identity. Validate those at runtime.
- `PlayerArray` elements are read as raw pointer-sized values; UE5 object-pointer representation must be confirmed.
- The repair identifies talkers conservatively and may find none. CachedPlayerState may not be reflected. Do not silently create guessed replacements.
- Property writes do not yet provide a complete transactional rollback path if the second write or registration fails.
- Readable memory is not proof that a UObject identity remains valid. The code avoids caching UObject pointers between callbacks but needs live lifecycle checks.
- Engine/device-map preflight and native switch operations have narrow SEH boundaries; this is not a general safety guarantee.
- A successful ChangeDevice/Start return has not been shown to correspond to successful transmitted microphone audio.
- No UI, persistence, logging initialization, saved-preference behavior, or automatic device reapplication is wired up.
- No audible proximity success has been demonstrated.

Do not expand this into a general modding framework, force Windows default-device changes, scan arbitrary process heaps for candidate captures, or replace the voice backend without a clear need and user authorization.

## Local research files and toolchain

Most research artifacts are outside the repository. They are referenced here so a successor can use them without repeating the work.

Workspace:

```text
C:\Users\Tim\Documents\Codex\2026-10-29\fge
```

Detailed microphone writeup:

```text
outputs\RV-microphone-investigation.md
```

Old packaged Lua prototype (not the requested C++ replacement):

```text
outputs\RVProximityFix-0.1.0.zip
```

Research directory:

```text
work\mic-research\
```

Most useful files there:

```text
voice-focused-analysis.txt
voice-focused-analysis-2.txt
voice-focused-analysis-3.txt
capture-ownership-analysis.txt
VoiceFocus.java
voice-focus-2.gpr
voice-focus-2.rep\
inspect-voice.cjs
voice-anchors.cjs
find-capture-functions.cjs
voice-strings.json
capture-function-anchors.json
capture-manager-refs.json
```

`voice-focused-analysis-2.txt` contains the ChangeDevice/CreateCaptureBuffer work. `capture-ownership-analysis.txt` contains manager/capture ownership evidence. These are focused Ghidra outputs, not a full-program audit.

Other workspace files:

```text
work\ue4ss-release.json
work\ue4ss-exports.txt
work\pinned-CppUserModBase.hpp
work\ue4ss.gitmodules
work\rv-voice-native\src\     (scratch originals of the seven copied C++ files)
work\rv-mod-fix\              (older staging copy of the Lua repo)
```

MSVC Build Tools:

```text
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe
VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\lib.exe
VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe
Common7\Tools\VsDevCmd.bat
```

A planned build route was generating an import `.def` from the downloaded DLL's exact decorated exports, producing an import library with `lib.exe`, then compiling/linking with MSVC. No such build has been run.

Ghidra:

```text
C:\Standalone Programs\_tools\Ghidra
```

Version observed: 12.1.2. Java:

```text
C:\Program Files\Eclipse Adoptium\jdk-21.0.10.7-hotspot
```

The retained Ghidra project allows quick focused decompilation without reimporting the game. Example template (fill output path/address deliberately):

```powershell
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-21.0.10.7-hotspot'
& 'C:\Standalone Programs\_tools\Ghidra\support\analyzeHeadless.bat' `
  'C:\Users\Tim\Documents\Codex\2026-10-29\fge\work\mic-research' 'voice-focus-2' `
  -process 'Ride-Win64-Shipping.exe' -noanalysis `
  -scriptPath 'C:\Users\Tim\Documents\Codex\2026-10-29\fge\work\mic-research' `
  -postScript VoiceFocus.java 'C:\path\to\output.txt' 0xADDRESS
```

A REA CLI 6.7.0 session was tried earlier; full binary analysis timed out, the session was closed, and raw x64 input was unsupported by that Windows route. The Ghidra results were the useful fallback. No persistent game instrumentation was left running by those sessions.

## Source links

Official UE4SS release page:

https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/experimental-latest

Pinned public C++ loader header:

https://raw.githubusercontent.com/UE4SS-RE/RE-UE4SS/cdbc0b66/UE4SS/include/Mod/CppUserModBase.hpp

UE4SS documentation:

https://docs.ue4ss.com/

Community issue reports consulted (reports support existence of symptoms, not proof of root cause):

https://steamcommunity.com/app/3949040/discussions/0/599667160179022779/
https://steamcommunity.com/app/3949040/discussions/1/681860928677934436/
https://steamcommunity.com/app/3949040/discussions/0/599667160179351483/
https://steamcommunity.com/app/3949040/discussions/0/599667160179032865/

## Validation status and minimum practical test

Completed earlier: source/log inspection, executable identification/hash, focused static capture analysis, and old Lua mocked tests. Completed now: copying seven draft C++ files into the requested repository and writing this handoff.

**Not completed:** native compilation, linking, loader compatibility, UI testing, live capture switching, reset repair, or multiplayer audible verification.

One person can validate native loading, dropdown population, and switching their own transmitted input using an appropriate test setup. They cannot establish the actual remote player's perceived distance/direction repair merely by loading the mod solo. The decisive proximity test needs another player or a second independently running client/account/setup.

A useful multiplayer test compares voice before reset, after reproducing the broken reset state, and after manual repair, using a remote player at different positions and distances. Confirm the defect first. A registration log is not an audible pass.

## Workflow correction

The failure in this attempt was spending too long on research/staging and describing intentions before producing an executable result in the requested repo. A useful guardrail for the next attempt is: make the smallest source change in the real repo, compile immediately, report the concrete result, then add one feature at a time. Keep optional research out of that critical path.
