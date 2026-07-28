# Nioh1Fix

Nioh1Fix is an experimental ASI plugin that raises **Nioh: Complete Edition**
from its 60 FPS gameplay profile to a configurable target. The default is
120 FPS.

The plugin is a Windows x64 DLL loaded by Proton. It is built and packaged on
Windows, but the release package runs on Linux through Proton.

## Status

- Supported executable: Steam Nioh v1.24.07, dated 2022-08-25.
- Supported SHA-256:
  `56006af3fc0945248aa7a2e33fd95d4e510f1dbe3395eb3644dae3c2806377f6`
- Default target: 120 FPS.
- The 30 FPS profiles used by lower-framerate and cinematic paths are not
  changed.
- Static analysis and automated tests are complete. Gameplay validation on
  Proton and Windows is still required before calling this a stable release.

Unsupported executable versions are rejected without modifying memory.

## Installation on Linux

1. Build the `windows-package` workflow or download a release package.
2. Extract `Nioh1Fix.asi`, `Nioh1Fix.ini`, and `version.dll` into the Nioh
   directory beside `nioh.exe`.
3. Add this to the game's Steam launch options:

   ```text
   WINEDLLOVERRIDES="version=n,b" %command%
   ```

4. Select the game's 60 FPS mode. Set VSync and the display refresh rate so
   they do not impose a separate 60 Hz limit.
5. Check `Nioh1Fix.log` after launch. A successful load reports both gameplay
   profiles patched to the configured target.

For the installation used during development, the game directory is:

```text
/mnt/ssd/SteamLibrary/steamapps/common/Nioh
```

The repository does not alter that directory during build or verification.

## Configuration

```ini
[Framerate]
Enabled = true
TargetFPS = 120
```

`TargetFPS` accepts 60 through 360. Values above 120 are available for testing,
not asserted to be free of engine-side animation or physics issues.

## Build

Native Linux builds compile and run the profile-table tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Build and package the ASI on Linux with a pinned portable LLVM-MinGW toolchain:

```bash
scripts/build-linux.sh
```

This creates `dist/Nioh1Fix-0.1.0.zip` without installing system packages.

Alternatively, build with Visual Studio 2022:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
./scripts/package.ps1
```

The package script downloads Ultimate ASI Loader v9.7.1 and renames its x64
proxy to `version.dll`. Toolchain and loader archives are checksum-verified.

## Read-only executable check

```bash
python3 scripts/verify_executable.py \
  "/mnt/ssd/SteamLibrary/steamapps/common/Nioh/nioh.exe"
```

This reports PE metadata, the SHA-256 digest, and the number of compatible frame
profile tables. It does not write to the executable.

## Uninstall

Remove `Nioh1Fix.asi`, `Nioh1Fix.ini`, `Nioh1Fix.log`, and `version.dll`. Also
remove the `WINEDLLOVERRIDES` launch option if no other mod needs it.

## Technical notes

See [docs/research.md](docs/research.md) for the limiter and timing analysis.

## Credits

- Lyall's Nioh2Fix and other Katana Engine fixes for the ASI/Proton packaging
  pattern.
- ThirteenAG's Ultimate ASI Loader for ASI loading.

Nioh1Fix is not affiliated with Koei Tecmo or Team Ninja.
