# Development

## Repository layout

| Path | Purpose |
| --- | --- |
| `.github/workflows/` | GitHub-hosted push builds and downloadable artifacts |
| `src/` | C++ mod sources and the pinned UE4SS ABI declarations |
| `tests/` | Native checks for the overlay, settings, re-apply policy, and guards |
| `scripts/` | Formatting, build, packaging, and installation helpers |
| `docs/` | Current development, testing, evidence, and status notes |
| `docs/history/` | Superseded handoff and Lua investigation |
| `out/` | Ignored generated files: compiler output and packages |

## GitHub builds

Push a branch to run `.github/workflows/build.yml`. It uses GitHub's
`windows-2022` image and its installed Visual Studio 2022 toolchain. CI installs
clang-format 19.1.5, runs the checked packaging command, and uploads the ZIP as a
direct artifact for 14 days. No secrets, CMake, game files, or UE4SS download
are required. The workflow also accepts manual dispatch.

The two GitHub actions are pinned to reviewed release commits. Build script
toolchain discovery uses `vswhere` instead of a machine-specific installation
path. A hosted run must still be observed after these changes are pushed.

## Local tools

Local compilation requires Visual Studio 2022 C++ Build Tools (x64) and a Windows
SDK. CMake and Node.js are not used. The build uses C++20, Release `/MD`, and
iterator debug level 0. `src/UE4SS.def` records only the 33 required decorated
imports from the pinned loader. `lib.exe` generates the import library from
that file; compilation does not require downloading UE4SS. The runtime still
checks the exact loader and game hashes in README.md.

Optionally pass `-UE4SSDll <path>` to verify both the loader hash and every
import definition against a local copy of the supported DLL. This is useful
when reviewing ABI changes; it is not required for normal builds.

## Formatting and analysis

Use clang-format **19**, with the repository's `.clang-format`. Windows SDK
include order is intentional, so the formatter does not reorder includes.
`.editorconfig` and `.gitattributes` keep indentation and line endings consistent.

```powershell
.\scripts\Format-Code.ps1
.\scripts\Format-Code.ps1 -Check
.\scripts\Build-Native.ps1 -Analyze -Test
```

The formatter supports `-ClangFormatPath` when clang-format is not on PATH or
in the standard Build Tools installation. Compiler `/W4` warnings are errors
(`/WX`). `-Analyze` enables MSVC static analysis; analysis warnings also fail
the build. No blanket warning suppressions are used.

The native checks run the pure panel content (`Overlay.cpp`: rows, structure
signature, slider snapping), the settings file, the saved-microphone re-apply policy, real Windows
device enumeration, and basic guards. A machine without capture devices uses two
fixture devices for the overlay checks. No check invokes
the game's capture functions or requires a game installation. These checks do
not establish live microphone or multiplayer correctness.

## Packaging

```powershell
.\scripts\Build-Package.ps1
```

Packaging requires formatting, static analysis, and native checks to pass. The
output is `out/dist/RVVoiceFix-0.3.0-test.zip`. Compiler/intermediate files stay
in `out/build/`; the mod tree is `out/dist/RVVoiceFix/`. The ZIP contains the mod,
current documentation, and license. It excludes the UE4SS loader and historical
research files.

Before packaging a source change, run the same formatting, analysis, and checks
that CI runs. Keep runtime behavior changes separate from formatting-only edits.
