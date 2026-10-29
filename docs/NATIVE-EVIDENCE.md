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
The x64 Release base is 192 bytes. `src/UE4SS.def` records the 33 imported
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

The legacy engine-tick and ProcessEvent hooks have no unregister handle. This
DLL is pinned and callback state remains until process exit. Uninstall disables
work; restart the process to replace the DLL. Voice, microphone and input work
runs in the engine post-tick callback. The ProcessEvent post-callback compares
the function pointer with one atomic value (non-null only while something is
visible) and otherwise returns. It draws only when the function is the current
local HUD's `ReceiveDrawHUD` and the context is that HUD. Both callbacks share
one recursive mutex taken with `try_lock`, so neither blocks the game. UI
actions contain values (device GUID and name, player name), not UObject
pointers.

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
