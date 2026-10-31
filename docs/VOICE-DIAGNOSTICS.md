# Voice diagnostics (investigation, 29 October 2026)

Possible per-player diagnostics. Not implemented beyond the F8 snapshot; no
game result.

## Candidate data

| Value | Source | Limit |
| --- | --- | --- |
| Player distance | `GetDistanceTo` / actor positions | Player-to-player, not source-to-listener |
| Speaking level | `UVOIPTalker::GetVoiceLevel()` | Activity meter, zero when silent; not output level |
| Wanted attachment and attenuation | `UVOIPTalker.Settings` | Not proof of the playing stream's settings |
| Playing component | `BPOnTalkingBegin(UAudioComponent*)` | Needs a runtime check |
| Applied attenuation | `BP_GetAttenuationSettingsToApply` | Check the parameter layout first |
| Component gain and spatialization | `VolumeMultiplier`, `bAllowSpatialization` | A multiplier of 1 does not mean no attenuation |
| Applied distance gain | Native `FWaveInstance` | No safe access for this build |

References: [UVOIPTalker](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UVOIPTalker)
(changed Settings apply at the next speech),
[BP_GetAttenuationSettingsToApply](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UAudioComponent/BP_GetAttenuationSettingsToApply),
[UAudioComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UAudioComponent),
[FBaseAttenuationSettings](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FBaseAttenuationSettings),
[FWaveInstance](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FWaveInstance).
These describe current Unreal; check each against Ride's 5.6 runtime.

## Executable scan

A read-only string scan of build 1.3.0.20702 (SHA-256
`12b24146b30d550fe47a4179c7b05d1f21b69f058524e0bba4b41397e3e66221`) found
`GetVoiceLevel`, `BPOnTalkingBegin`, `BP_GetAttenuationSettingsToApply`,
`GetDistanceTo`, `au.3dVisualize.Enabled`, `au.AllowAudioSpatialization` and
`au.DisableDistanceAttenuation`. A name in the file does not prove a callable
function.

It did not find `au.Debug.Sounds`, `au.DumpActiveSounds` or the other
[audio debug commands](https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-console-commands-in-unreal-engine?application_version=5.6).
They may be absent from Shipping builds. Do not promise a stock debug view.

## Proposed F8 table

One read-only row per player: identity, distance, speaking level, wanted and
playing attachment, attenuation, spatialization, gain, playback state. Take it
before a reset, after a fault and after F9. Mark missing data
**unavailable** and computed gain **estimated**. Show applied gain only after a
verified native route exists. An estimate needs the real attenuation shape,
curve, source and listener positions; do not assume linear falloff.
Rate-limit sampling; no per-frame object scans.
