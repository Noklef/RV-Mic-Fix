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

| Item | RVA/layout |
| --- | --- |
| Manager pointer / capture vtable | `0xa6c7210` / `0x8c1fd18` |
| ChangeDevice | `0x4e352e0` |
| Name hash / map lookup | `0x1097ca0` / `0x3077020` |
| Capture internal pointer / state | `+0xa0` / `+0xb0` |
| Internal GUID / device / buffer | `+0` / `+0x10` / `+0x28` |
| Channels / rate / bits | `+0x32` / `+0x34` / `+0x3e` |
| Manager map / entry stride / entry GUID | `+0x58` / `0x40` / `+0x20` |

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
The x64 Release base is 192 bytes. The import library comes from the exact
DLL's decorated exports. The official header requires a matching C runtime and
configuration. Layout size and successful linking are necessary checks, not
a complete runtime ABI test.

The [pinned loader implementation](https://raw.githubusercontent.com/UE4SS-RE/RE-UE4SS/cdbc0b66/UE4SS/src/Mod/CppMod.cpp)
loads `dlls/main.dll` and accepts a null `start_mod` result as not started. This
mod returns null on hash mismatch before constructing the loader base or
invoking any native/Unreal function.

The legacy tick hook has no unregister handle. This DLL is pinned and callback
state remains until process exit. Uninstall disables work and stops the UI;
restart the process to replace the DLL. UObject/native work runs only in the
engine post-tick callback. UI commands contain values, not UObject pointers.

Named object flags reject ClassDefaultObject `0x10`, ArchetypeObject `0x20`,
BeginDestroyed `0x8000`, and FinishDestroyed `0x10000`, checked against the
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

No UObject pointers survive callbacks. Resets preserving all observed player
identities may require F9. Registration may leave active remote playback
unchanged; no audible success is claimed.
