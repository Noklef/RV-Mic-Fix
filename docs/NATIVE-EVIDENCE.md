# Native evidence

The supported hashes are in README.md, `src/Common.hpp` and
`scripts/Build-Native.ps1`. Addresses are module base + RVA. Do not pin a new hash before the layouts and calls are
checked again.

## Microphone switch

Evidence folder:
`C:\Users\Tim\Documents\Codex\2026-10-29\fge\work\mic-research\`

| File | Finding |
| --- | --- |
| `voice-focused-analysis-2.txt` | ChangeDevice: checks state, stops and releases capture, resolves the name, creates 16-bit PCM capture, returns success in the low byte. Arguments: capture, name, sample rate, channels. |
| `voice-focused-analysis-3.txt` | Start sets state 3 and returns success; Stop sets state 4. |
| `capture-ownership-analysis.txt` | Capture layout; how the manager adds and removes captures. |

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

Runtime checks: exactly one initialized, manager-owned capture with the exact
vtable; target and rollback names both map to the expected GUID; supported
state, live buffers, 16-bit PCM, 8-48 kHz, one or two channels. Format is kept,
and capture is restarted only if it was active. Success and rollback check
GUID, format, buffers and state. A native fault disables further switching.
None of this proves that the capture feeds network voice.

### Re-location method (after a game update)

1. ChangeDevice is the only function with a RIP-relative reference to the
   string "Unable to change device, not initialized".
2. Compare the saved old code bytes with the new image at the same shift.
   Every differing byte must lie in a call, jump or RIP-relative field.
   `relocate.cjs <exe> <old-bin> <old-load-hex> <oldChange>` decodes these
   fields and maps old targets to new ones. It rejects a decode whose new
   target is outside a section or whose old target is outside the old image.
3. The vtable is the one whose slot 5 points to the new ChangeDevice.

### CL-21905 (30 October 2026)

Image `904ba24cab1c3edf69100d267e570e6889445a2bfa5503803207d62c80a9820a`.
Files: `relocate-cl21905.json`, `vtable-cl21905.txt`,
`capture-device-code-cl21905.bin`.

- ChangeDevice +0x520 (string reference `0x4e3581f`).
- 3004 of 3344 bytes identical; the rest are in rel32 fields. Globals moved
  +0xe7a0. This run used the first `relocate.cjs`, which could accept false
  starts; the string reference and vtable confirmed the anchors independently.
- Vtable `0x8c24eb8`: its only two `lea` references are the old ones +0x520.
  Slots 3 and 4 are the old Start and Stop +0x520.

### CL-21945 (31 October 2026)

Image `8c229aded5833756b124e9bbad59871b4417f3a7af26b55f194cd448df5537da`.
Files: `relocate-cl21945.json`, `vtable-cl21945.txt`,
`capture-device-code-cl21945.bin`.

- ChangeDevice +0x450 (string reference `0x4e35c6f`).
- 2947 of 3344 bytes identical; every other byte is in one of 160 decoded
  fields with a valid target. Code +0x450, `.rdata` +0x1000, globals +0x3390,
  +0x3f00, +0x4000. The first scanner had accepted 32 false starts here; the
  fixed one rejects them.
- Manager +0x4000 (its neighbours moved the same). Name hash and map lookup
  did not move.
- Vtable `0x8c28248`: all eight slots and both `lea` references are the
  CL-21905 ones +0x450.

## Loader ABI

- Declarations follow the public
  [CppUserModBase header at cdbc0b66](https://raw.githubusercontent.com/UE4SS-RE/RE-UE4SS/cdbc0b66/UE4SS/include/Mod/CppUserModBase.hpp).
  The x64 Release base is 192 bytes (static assert).
- `src/UE4SS.def` lists 35 decorated imports, checked against the pinned DLL.
  Linking and layout size are necessary checks, not a full ABI test.
- The [loader](https://raw.githubusercontent.com/UE4SS-RE/RE-UE4SS/cdbc0b66/UE4SS/src/Mod/CppMod.cpp)
  treats a null `start_mod` result as not started. On a hash mismatch the mod
  returns null before it touches UE4SS or the game.
- The engine-tick hook cannot be unregistered. Restart the game to replace the
  DLL.
- Stable 3.0.1 lacks `RegisterEngineTickPostCallback` and times out scanning
  EngineVersion, GUObjectArray and FText in this game, so the experimental
  build is required.

Value imports:

| Import | Use |
| --- | --- |
| `UClass::GetClassFlags` | Clear `CLASS_Abstract` (`0x1`) on `UserWidget` for one `Create` call |
| `FProperty::DestroyValue` | Release returned `FText`, `FString`, `TArray` and struct values |
| `FProperty::InitializeValue` | Construct a return slot whose type has a vtable (`FUniqueNetIdRepl`) before a call. The call buffer is only zero-filled. |
| `FProperty::CopyCompleteValue` | Copy the game list's style into the tab's lists |

Structs the mod writes (`FLinearColor`, `FMargin`, `FVector2D` as 2 doubles)
are size-checked against the reflected parameter. `FSlateColor` is copied from
the live value; only `SpecifiedColor` is replaced.

Object checks reject the flags ClassDefaultObject `0x10`, ArchetypeObject
`0x20`, BeginDestroyed `0x8000`, FinishDestroyed `0x10000` and MirroredGarbage
`0x40000000`
([flag reference](https://docs.ue4ss.com/lua-api/table-definitions/eobjectflags.html)),
and unreachable objects. Lookups of default objects (`ue::find`) must not use
`ue::live()`. Flags and readable memory do not prove object identity.

Loading, the first tick callback and F7 are separate log lines. A successful
build proves none of them.

UE4SS's virtual `SetObjectPropertyValue` is unsupported in this engine and
throws. Object properties are written as one raw pointer and read back with
`GetObjectPropertyValue`. If `DestroyValue` throws, the mod logs it once and
stops releasing `FText` (a small leak).

## Proximity repair limits

Matching is conservative: a missing or ambiguous talker is skipped. Missing
attenuation is restored only if `SA_VOIP` is already loaded. A failed property
write is rolled back if possible; a registration cannot be undone. The repair
runs only with host authority.

## Game Sound menu (Redpoint voice chat)

`WG_AudioMenu` uses Redpoint EOS voice chat, not the capture the mod switches:

| Function | Parameters |
| --- | --- |
| `RedpointFrameworkBlueprintLibrary:GetAvailableVoiceChatAudioDevices` | `WorldContextObject` -> `TArray<FRedpointVoiceChatAudioDevice>` |
| `…:GetVoiceChatAudioInputDevice` | `WorldContextObject`, `PlayerId` (`FUniqueNetIdRepl` @0x8) -> device @0x38 |
| `…:SetVoiceChatAudioInputDevice` | `WorldContextObject`, `PlayerId` @0x8, `DeviceID` (`FString` @0x38) |
| `OnlineHelpers:GetControllerUniqueNetId` | `PlayerController` -> `FUniqueNetIdRepl` @0x8 |

`FRedpointVoiceChatAudioDevice`: `DeviceID` @0x0, `DeviceName` @0x10 (both
`FString`), `bIsInputDevice` @0x20. `RGGameUserSettings` stores only mic gain
and noise gate; no device name is saved.

Result (29 October 2026, single player): Redpoint returned no devices
("No online services are present"). "Input Device" equals the Windows default
recording device, so it is a label, not a setting. Indirect evidence that the
game's choice drives the native capture at start: with a different saved device,
the mod re-applied about 10 s after load; with the same device, it did not.
Not tested: whether `SetVoiceChatAudioInputDevice` moves the native capture
during play (MIC-DIAG, TESTING step 2d).
