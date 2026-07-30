# Nioh1Fix

Nioh1Fix is an ASI plugin that unlocks the framerate of **Nioh: Complete
Edition** while keeping gameplay and animation speed stable as the actual
framerate changes.

The plugin is a Windows x64 DLL. The release package runs on Linux through
Proton and includes the required ASI loader.

## Status

- Current version: 1.4.0.
- Supported executable: Steam Nioh v1.24.07, dated 2022-08-25.
- Supported executable SHA-256:
  `56006af3fc0945248aa7a2e33fd95d4e510f1dbe3395eb3644dae3c2806377f6`
- Validated on Linux/Proton at 130 FPS and while changing external framerate
  caps during gameplay.
- The original 30 FPS profiles remain capped and use their stock timing.

Unsupported executable versions are rejected without modifying memory.

## Installation on Linux

1. Extract the release archive into the Nioh directory beside `nioh.exe`.
2. Add this to the game's Steam launch options:

   ```text
   WINEDLLOVERRIDES="version=n,b" %command%
   ```

3. Select Nioh's 60 FPS mode.
4. Apply any desired external FPS cap, for example through MangoHud.
5. Start the game and inspect `Nioh1Fix.log` if needed.

Nioh has no in-game VSync toggle. The mod removes the engine and presentation
limits, but the desktop, compositor, driver, or an external limiter can still
limit the resulting framerate.

The development installation used for testing is:

```text
/mnt/ssd/SteamLibrary/steamapps/common/Nioh
```

## Configuration

```ini
[Framerate]
Enabled = true
TargetFPS = 120
```

- `Enabled = false` prevents all Nioh1Fix memory patches.
- `TargetFPS` accepts values from 60 through 360.
- `TargetFPS` configures Nioh's internal gameplay profile and initial timing
  fallback. It is not the final external FPS cap.
- Leave `TargetFPS = 120` unless testing engine behavior. Control the actual
  framerate with an external limiter or by leaving it uncapped.

The animation correction measures presentation intervals with
`QueryPerformanceCounter`. It therefore supports arbitrary and changing
framerates rather than assuming 120 or 130 FPS.

## Build on Linux

Build and run the platform-independent profile-table tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Cross-compile the ASI with LLVM-MinGW:

```bash
cmake -S . -B build-windows \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=/path/to/llvm-mingw/bin/x86_64-w64-mingw32-clang++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows
scripts/package.sh build-windows dist
```

The package script creates `dist/Nioh1Fix-1.4.0.zip`. It downloads Ultimate
ASI Loader v9.7.1, verifies its SHA-256 digest, and renames the x64 proxy to
`version.dll`.

## Build on Windows

With Visual Studio 2022:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
./scripts/package.ps1
```

## Read-only Executable Check

```bash
python3 scripts/verify_executable.py \
  "/mnt/ssd/SteamLibrary/steamapps/common/Nioh/nioh.exe"
```

This reports PE metadata, the SHA-256 digest, and compatible frame-profile
table matches. It does not modify the executable.

## Disable or Uninstall

To disable all patches without removing files:

```ini
[Framerate]
Enabled = false
TargetFPS = 120
```

Restart the game after changing the setting.

To uninstall, remove `Nioh1Fix.asi`, `Nioh1Fix.ini`, and `Nioh1Fix.log`.
Remove `version.dll` and the `WINEDLLOVERRIDES` launch option only if no other
mod uses that ASI loader.

## Technical Notes

See [docs/research.md](docs/research.md) for the reverse-engineered limiter and
timing paths. See [AGENTS.md](AGENTS.md) for the implementation handoff and
development constraints.

## Credits

- Lyall's Katana Engine fixes for the ASI and Proton packaging patterns.
- ThirteenAG's Ultimate ASI Loader for ASI loading.

Nioh1Fix is not affiliated with Koei Tecmo or Team Ninja.
