# Evidence and implementation boundary

> Historical Lua-prototype notes. Current native implementation and limitations
> are described in [NATIVE-EVIDENCE.md](../NATIVE-EVIDENCE.md) and [STATUS.md](../STATUS.md).

## Observed

- The supplied 26 October 2026 session log identifies Ride build
  `1.3.0.20702`, Unreal 5.6, a Steam listen server, and crossplay disabled.
- Voice decoding continues after same-map server travel/checkpoint reloads.
- Voice components unregister around those reloads. This is normal lifecycle
  activity and does not, by itself, prove a registration failure.
- The host confirmed that proximity worked before a checkpoint reset and failed
  afterward, and ruled out Voicemeeter as the cause.
- Read-only shell inspection of the installed executable's version information
  matched build `Ride-CL-20702-6ab26b87853e185d15f6f574`.
- The installed `Manifest_UFSFiles_Win64.txt` lists these package entries:
  - `Ride/Content/Ride/Blueprints/BP_VOIPTalker.uasset` (line 5191)
  - `Ride/Content/Ride/Audio/Attenuation/SA_VOIP.uasset` (line 6510)
  - `Ride/Content/Ride/Audio/SoundClasses/SC_VoiceChat.uasset` (line 6618)

Manifest entries establish packaging intent, not inheritance or actual runtime
use. The cooked Blueprint has not been decompiled. No REA Evidence IDs were
generated: REA tools were not connected and the CLI package was unavailable in
the sandbox's offline npm cache. The observations above came from ordinary file
inspection, not native decompilation or runtime tracing.

## Working hypothesis

The host loses a current talker association, attachment, or attenuation setup
when the checkpoint replaces actors. Existing network voice reception survives.
This is consistent with the symptom but not a proven internal root cause. In
particular, stale native playback objects may require a different repair.

The prototype uses reflected engine-level APIs only. It discovers all objects
anew during a game-thread pass, matches the current roster exactly, and skips
ambiguous cases. It neither invokes unknown Blueprint functions nor fabricates
network/player identities. Timers retain no game object references.

## Sources consulted

- [Player reports of host proximity failing after resets](https://steamcommunity.com/app/3949040/discussions/0/599667160179022779/)
- [Checkpoint bug report](https://steamcommunity.com/app/3949040/discussions/1/681860928677934436/)
- [Epic: UVOIPTalker](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UVOIPTalker)
- [Epic: RegisterWithPlayerState](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UVOIPTalker/RegisterWithPlayerState)
- [Epic: UVOIPStatics lifecycle API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UVOIPStatics)
- [UE4SS game-thread execution](https://docs.ue4ss.com/dev/lua-api/global-functions/executeingamethread.html)
- [UE4SS delayed action API](https://github.com/UE4SS-RE/RE-UE4SS/blob/main/docs/upgrade-guide.md)
- [UE4SS reflected UObject access](https://docs.ue4ss.com/dev/lua-api/classes/uobject.html)
- [UE4SS construction notifications](https://docs.ue4ss.com/dev/lua-api/global-functions/notifyonnewobject.html)
- [UE4SS FindAllOf behavior and cost](https://docs.ue4ss.com/dev/lua-api/global-functions/findallof.html)
- [UE4SS weak-pointer access](https://docs.ue4ss.com/lua-api/classes/fweakobjectptr.html)

Some current Epic reference pages describe engine versions newer than the
target. Actual reflection support and loader compatibility remain live-test
requirements for build 20702.

## Remaining unknowns

1. Whether BP_VOIPTalker inherits the native talker or wraps it.
2. Which property or association actually changes during the failing reset.
3. Whether registration refreshes an already active decoded stream.
4. Whether this exact game and UE4SS build support every reflected read/write.
5. Whether the same behavior occurs with crossplay enabled or other game builds.

The F8 snapshots and first-run test procedure are intended to resolve these
questions. The current deliverable is an experimental repair candidate, not a
confirmed patch.
