# Native evidence and boundaries

Supported image hashes are in README.md and src/Common.hpp. Runtime addresses
use module base + RVA. Never accept another hash without checking the layouts
and calling conventions again.

## Retained static evidence

Prior Ghidra reports are in:
`C:\Users\Tim\Documents\Codex\2026-10-29\fge\work\mic-research\`

| Evidence | Observation |
| --- | --- |
| `voice-focused-analysis-2.txt`, `144e352e0` | ChangeDevice checks state, stops/releases capture, resolves the name, creates 16-bit PCM capture, returns low-byte success. Arguments: capture, FString-like name, sample rate, channels. |
| `voice-focused-analysis-3.txt`, `144e39190` / `144e39220` | Start returns success and sets state 3; Stop sets state 4. |
| `capture-ownership-analysis.txt` | Capture constructor layout and manager capture-list insertion/removal. |

| Item | 1.3.0.20702 | CL-21905 | CL-21945 |
| --- | --- | --- | --- |
| Manager pointer | `0xa6c7210` | `0xa6d59b0` | `0xa6d99b0` |
| Capture vtable | `0x8c1fd18` | `0x8c24eb8` | `0x8c28248` |
| ChangeDevice | `0x4e352e0` | `0x4e35800` | `0x4e35c50` |
| Name hash / map lookup | `0x1097ca0` / `0x3077020` | `0x1097f60` / `0x30771d0` | same |
| Capture internal pointer / state | `+0xa0` / `+0xb0` | same | same |
| Internal GUID / device / buffer | `+0` / `+0x10` / `+0x28` | same | same |
| Channels / rate / bits | `+0x32` / `+0x34` / `+0x3e` | same | same |
| Manager map / entry stride / entry GUID | `+0x58` / `0x40` / `+0x20` | same | same |

### Re-location in CL-21905 (30 October 2026)

`relocate-cl21905.cjs` and `vtable-cl21905.cjs` with their output
`relocate-cl21905.json` and `vtable-cl21905.txt` are in the evidence folder
above, with the new code bytes `capture-device-code-cl21905.bin`. Image SHA-256
`904ba24cab1c3edf69100d267e570e6889445a2bfa5503803207d62c80a9820a`.

- ChangeDevice: the only function with a RIP-relative reference to the string
  descriptor of "Unable to change device, not initialized" (`0x4e3581f` in
  `0x4e35800`), 0x520 after the old one.
- The 3344 bytes of `capture-device-code.bin` (old `0x144e34e90`) compared
  with the new image at +0x520: 3004 identical, and every other byte lies in a
  call, jump or RIP-relative displacement. Immediates and field offsets are
  therefore unchanged. Decoding the same instructions on both sides maps the
  manager, name hash and map lookup to their new targets; neighbouring globals
  move by the same amount (+0xe7a0). This run used the first `relocate.cjs`,
  which could accept false starts; the anchors were also found independently
  (string reference, vtable).
- Capture vtable: the two old vtable loads (`0x144e3479f`, `0x144e34a3f`) at
  +0x520 load `0x8c24eb8`, and these are its only two `lea` references. Slot 5
  is the new ChangeDevice; slots 3 and 4 are the old Start/Stop at +0x520.

### Re-location in CL-21945 (31 October 2026)

Same method, from `capture-device-code-cl21905.bin` (`0x144e353b0`) with
`relocate.cjs` (old ChangeDevice as an argument). Output
`relocate-cl21945.json` and `vtable-cl21945.txt`; new code bytes
`capture-device-code-cl21945.bin` (`0x144e35800`). Image SHA-256
`8c229aded5833756b124e9bbad59871b4417f3a7af26b55f194cd448df5537da`.

- ChangeDevice: the only string reference is `0x4e35c6f` in `0x4e35c50`
  (+0x450).
- 2947 of 3344 bytes identical. Every other byte lies in one of 160 decoded
  call, jump or RIP-relative fields whose new target is inside a section.
  `relocate.cjs` now rejects a decode with an impossible target and tries the
  next start; the first version accepted 32 false starts (for example inside a
  `lea`, or on `75 25` and `eb 3d`). Shifts: code +0x450 and nearby, `.rdata`
  +0x1000 (`.text` grew), globals +0x3390 / +0x3f00 / +0x4000.
- Manager `0xa6d59b0` -> `0xa6d99b0`; the other globals beside it
  (`0xa6d59c8`, `0xa6d59d8`, `0xa6d59f8`) also +0x4000. Name hash and map
  lookup did not move.
- Vtable `0x8c28248`: found by its slot-5 pointer to the new ChangeDevice. All
  eight slots are the CL-21905 slots +0x450; its only two `lea` references are
  the CL-21905 ones +0x450.

This is static evidence. The runtime checks (one manager-owned capture with
the exact vtable, both device names mapping to the expected GUID) still apply.

These are static observations, not recorded executions. The mod requires one
initialized manager-owned capture with the exact vtable. Runtime testing must
still prove that this capture feeds network voice. Ambiguous captures are
refused. Both target and rollback names must map to the expected GUID.

The mod requires supported state, live buffers, and 16-bit PCM at 8-48 kHz with
one/two channels. It preserves format and restarts only previously active
capture. Success and rollback check GUID, format, buffers, and state. A native
fault disables further switching. These checks cannot prove transmitted audio.

## Loader ABI

Declarations follow the public
[CppUserModBase header at cdbc0b66](https://raw.githubusercontent.com/UE4SS-RE/RE-UE4SS/cdbc0b66/UE4SS/include/Mod/CppUserModBase.hpp).
The x64 Release base is 192 bytes. `src/UE4SS.def` records the 34 imported
decorated names verified against this exact DLL. The build generates an import
library from that checked-in definition, with no loader download required.
Optional `-UE4SSDll` validation checks the hash and every listed export again.
The official header requires a matching C runtime and
configuration. Layout size and successful linking are necessary checks, not
a complete runtime ABI test.

The [pinned loader implementation](https://raw.githubusercontent.com/UE4SS-RE/RE-UE4SS/cdbc0b66/UE4SS/src/Mod/CppMod.cpp)
loads `dlls/main.dll` and accepts a null `start_mod` result as not started. This
mod returns null on hash mismatch before constructing the loader base or
invoking any native/Unreal function.

The legacy engine-tick hook has no unregister handle. This DLL is pinned and
callback state remains until process exit. Uninstall disables work; restart the
process to replace the DLL. Voice, microphone, input and UMG work all runs in
the engine post-tick callback, under one mutex taken with `try_lock`, so it
never blocks the game. UI actions contain values (device GUID and name, player
name), not UObject pointers.

The UMG overlay adds two imports. `UClass::GetClassFlags` clears
`CLASS_Abstract` (`0x1`) on `/Script/UMG.UserWidget` for the duration of one
`Create` call; see DESIGN.md. `FProperty::DestroyValue` releases the `FText`
returned by `Conv_StringToText` after `SetText` has copied it. UE4SS's own
`FText` uses the same property value operations. The struct layouts the mod
writes as call parameters are `FLinearColor` (4 floats), `FMargin` (4 floats)
and `FVector2D` (2 doubles in UE5). Each is size-checked against the reflected
parameter, so a mismatch fails the call instead of corrupting memory.
`FSlateColor` is copied from the live property and only its `SpecifiedColor`
member is replaced.

The original test DLL imported RegisterEngineTickPostCallback, which stable
3.0.1 does not export. Retargeting the mod to stable fixed that import but
did not fix the loader's game compatibility: the actual stable run timed out
scanning EngineVersion, GUObjectArray and FText before on_unreal_init. The user
then approved the experimental build. Loading, first callback and F7 reception
are separate log milestones; none is inferred from compilation.

Named object flags reject ClassDefaultObject `0x10`, ArchetypeObject `0x20`,
BeginDestroyed `0x8000`, FinishDestroyed `0x10000`, and MirroredGarbage
`0x40000000` (UE5 garbage marking), checked against the
[UE4SS flag reference](https://docs.ue4ss.com/lua-api/table-definitions/eobjectflags.html).
`IsUnreachable` is checked separately. Readable memory and these flags cannot
prove UObject identity; lifecycle/reflection validation in the game is pending.

## Proximity hypothesis

Stale attachment/registration after reset is still a hypothesis. The repair
checks current world, host authority and Pawn/PlayerState ownership, and matches
existing remote talkers conservatively. Missing/ambiguous talkers are skipped.
Missing attenuation is restored only if the game asset is already loaded.
Property-write failure attempts rollback. Registration cannot be undone as a
transaction, and native faults are not recoverable proof.

No raw UObject pointer is dereferenced after the callback that found it.
Volume targets between passes are `FWeakObjectPtr`, which resolve to null after
garbage collection. Pointer values used as map keys (restart limits) are never
dereferenced. Repair runs only with local host authority. No audible success is
claimed; see DESIGN.md for the GC hypothesis and the stream restart.

## In-game finding: UE4SS virtual property calls (29 October 2026)

The first UMG run threw `Virtual FObjectPropertyBase::SetObjectPropertyValue is
unavailable, possibly unsupported in engine version` when setting
`WidgetTree.RootWidget`. `Create` of the plain `UserWidget` (abstract flag
cleared), its `WidgetTree`, and `SpawnObject` of a `CanvasPanel` had all
succeeded before it. Object properties are now written as one raw pointer and
read back through `GetObjectPropertyValue`, which works. The import was removed.
The talker repair used the same call, so it would have failed the same way.
`FProperty::DestroyValue` is also a UE4SS virtual call. If it throws, the mod
logs it once and stops releasing `FText` copies (a small leak while the
overlay text changes).

## Game Sound menu input device (object dump, 29 October 2026)

`WG_AudioMenu` (Settings → Sound) does not read the capture that the mod
switches. It uses Redpoint EOS voice chat, all reflected:

| Function | Parameters |
| --- | --- |
| `/Script/RedpointEOSFramework.RedpointFrameworkBlueprintLibrary:GetAvailableVoiceChatAudioDevices` | `WorldContextObject` → `TArray<FRedpointVoiceChatAudioDevice>` |
| `…RedpointFrameworkBlueprintLibrary:GetVoiceChatAudioInputDevice` | `WorldContextObject`, `PlayerId` (`FUniqueNetIdRepl` @0x8) → device @0x38 |
| `…RedpointFrameworkBlueprintLibrary:SetVoiceChatAudioInputDevice` | `WorldContextObject`, `PlayerId` @0x8, `DeviceID` (`FString` @0x38) |
| `/Script/OnlineSubsystemBlueprints.OnlineHelpers:GetControllerUniqueNetId` | `PlayerController` → `FUniqueNetIdRepl` @0x8 |

`FRedpointVoiceChatAudioDevice`: `DeviceID` FString @0x0, `DeviceName`
FString @0x10, `bIsInputDevice` @0x20. The array element and the getter's
return value are the same struct (one `UScriptStruct` address in the dump), so
the getter's return property types the array elements. `RGGameUserSettings` holds mic gain and
noise gate only; no device name is stored in `Saved/Config` or `SaveGames`.

Indirect evidence that the game's setting drives the native capture at start:
with the mod's saved device different from the menu's, the mod logged a
`Saved microphone re-apply` about 10 s after load; with both equal, it did not.
Not yet tested: whether `SetVoiceChatAudioInputDevice` switches the same native
capture during play, and which path carries voice in multiplayer. `Ride.log`
also shows Redpoint voice chat components (`VoiceChatComponent`, voice chat
anchor point with an audio patch).

In-game result (29 October 2026): Redpoint returned no devices and no current
device (`devices: none`, `current '(none)'`); `Ride.log` reports "No online
services are present". The Settings -> Sound "Input Device" text equals the
Windows default recording device (console and communications roles were both
`Voicemeeter Out B1`), consistent with the menu's `GetDefaultCaptureDevice`
call. It is a read-only label, not a game setting, so the native capture
switch is the path that matters.
