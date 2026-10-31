# Development

## Layout

| Path | Contents |
| --- | --- |
| `src/` | Mod sources and the pinned UE4SS ABI declarations |
| `tests/` | Native checks (no game needed) |
| `scripts/` | Format, build, package and install helpers |
| `docs/` | Current notes; `docs/history/` holds superseded records |
| `out/` | Generated files (ignored) |

## Tools

- Visual Studio 2022 C++ Build Tools (x64) and a Windows SDK. No CMake.
- clang-format 19 (`-ClangFormatPath` if it is not on PATH). It does not sort
  includes: the Windows SDK order matters. `.editorconfig` and `.gitattributes`
  set indentation and line endings.

The build is C++20, Release `/MD`, `_ITERATOR_DEBUG_LEVEL=0`, `/W4 /WX`. `src/UE4SS.def` lists the 35
loader imports; `lib.exe` makes the import library from it, so no UE4SS
download is needed.

## Commands

```powershell
.\scripts\Format-Code.ps1           # format
.\scripts\Format-Code.ps1 -Check    # check only
.\scripts\Build-Native.ps1 -Analyze -Test
.\scripts\Build-Package.ps1         # out/dist/RVVoiceFix-0.3.0-test.zip
```

- `-Analyze` runs MSVC static analysis; its warnings also fail the build.
- `-UE4SSDll <path>` checks the loader hash and every import against a local
  DLL. Use it after any ABI change.
- Packaging runs format check, analysis and tests first. The ZIP holds the mod,
  the docs and the license, not UE4SS.

The native checks cover the overlay rows, slider snapping, the settings file,
the microphone re-apply policy, the hash gate, SHA-256 and real Windows device
enumeration (two fixture devices when the machine has none). They never call
the game.

Keep formatting-only edits separate from behaviour changes.

## CI

`.github/workflows/build.yml` runs on each push and on manual dispatch
(`windows-2022`, clang-format 19.1.5). It runs `Build-Package.ps1` and uploads
the ZIP for 14 days. It needs no secrets or game files. The actions are pinned
to release commits.

## After a game update

The mod logs `DISABLED: executable or UE4SS SHA-256 does not match`. Find the
microphone addresses again before pinning the new hash; see
[NATIVE-EVIDENCE.md](NATIVE-EVIDENCE.md). Update the RVAs in `src/Devices.cpp`
and the hash in `src/Common.hpp`, `scripts/Build-Native.ps1` and `README.md`.
