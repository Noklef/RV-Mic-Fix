# Voice diagnostics investigation

Investigated 29 October 2026. This describes feasible diagnostics; it does not
implement a new F8 panel or claim a live game result.

## Findings

Useful per-player data exists in Unreal's VOIPTalker route. The strongest first
comparison is configured talker settings versus the active playback component.

| Value | Candidate source | Boundary |
| --- | --- | --- |
| Player distance | Reflected `GetDistanceTo` / actor positions | Label player-to-player geometry separately from actual audio source-to-listener distance. |
| Speaking level | `UVOIPTalker::GetVoiceLevel()` | Activity/loudness meter; documented as zero when not talking. It does not establish final output level. |
| Desired attachment/attenuation | `UVOIPTalker.Settings` | Already partly logged by F8; not proof of the current stream's settings. |
| Active playback component | `BPOnTalkingBegin(UAudioComponent*)` | Candidate event for associating a talker with its playing sound component; needs runtime validation. |
| Effective attenuation | `BP_GetAttenuationSettingsToApply` | Distinguishes component overrides from an asset. Validate reflected parameter layouts first. |
| Component gain/spatialization | `VolumeMultiplier`, `bAllowSpatialization`, attenuation flags | A component multiplier of 1 alone does not imply missing distance attenuation. |
| Applied distance gain and playback gain | Native wave-instance data | Engine-level quantities; no safe accessor for this exact game build has been established. |

Epic's [VOIPTalker reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UVOIPTalker)
documents the meter, talking-begin event, and that changed Settings take effect
on the next speech event. This motivates inspecting existing playback after a
reset; it does not prove the defect's cause.

The [attenuation getter](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UAudioComponent/BP_GetAttenuationSettingsToApply)
and [audio component properties](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UAudioComponent)
describe the reflected route. These retrieved API pages describe current
Unreal; each candidate must be validated against Ride's 5.6 runtime.

## Installed executable evidence

A bounded, read-only ASCII/UTF-16 scan of the supported Shipping executable
found these names. Offsets below are decimal file offsets, not call addresses.

| Name | File offset |
| --- | --- |
| `GetVoiceLevel` | 144301296 |
| `BPOnTalkingBegin` | 144300200 |
| `BP_GetAttenuationSettingsToApply` | 138889904 |
| `GetDistanceTo` | 140553120 |
| `au.3dVisualize.Enabled` | 141038536 |
| `au.AllowAudioSpatialization` | 140950432 |
| `au.DisableDistanceAttenuation` | 137967232 |

Image SHA-256:
`12b24146b30d550fe47a4179c7b05d1f21b69f058524e0bba4b41397e3e66221`.
Name presence does not establish runtime registration, live objects, or safe
calling signatures.

The scan did not find exact names `au.Debug.Sounds`, `au.Debug.Sounds.Sort`,
`au.Debug.Sounds.ShowPath`, `au.DumpActiveSounds`, `au.3dVisualize.Attenuation`,
`au.3dVisualize.Listeners`, or `au.3dVisualize.ActiveSounds`. They are documented
in [Unreal 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-console-commands-in-unreal-engine?application_version=5.6),
but may be omitted from this Shipping build. This bounded negative scan cannot
exclude all alternate routes. No command was executed; a stock overlay must
not be promised.

## Expected versus actual

An expected distance-gain estimate needs the effective shape/model/curve,
source transform, listener/attenuation override position, and distance scale.
Do not assume linear spherical falloff. The engine's
[attenuation settings API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FBaseAttenuationSettings)
provides corresponding evaluation methods.

The current [FWaveInstance API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FWaveInstance)
lists applied distance gain, spatialization state, listener distance, and
`GetActualVolume`. Exact methods/layouts in Ride 5.6 are unverified. These are
native audio-engine data, not ordinary reflected properties. A correctly
associated audio-thread snapshot is needed; guessed offsets and UI-thread
reads would be inappropriate. A wave's calculated playback gain is also
different from a per-person measurement at physical speakers after mixing
and device-volume controls.

## Proposed F8 view

Use a read-only per-player table: identity, player distance, speaking meter,
desired versus active attachment, attenuation asset/override, spatialization
flags, component gain, and playback state. Capture before reset, after failure,
and after F9. Label missing data **unavailable** and computed gain **estimated**.
Only show applied gain after a verified native access route exists.

Rate-limit sampling while diagnostics are enabled. Avoid per-frame global
object scans and raw UObject pointers retained across resets. Existing F8
still writes a structural log snapshot; the table and gain instrumentation
are not implemented.
