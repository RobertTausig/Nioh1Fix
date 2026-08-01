# Nioh1Fix

Nioh1Fix is an ASI plugin that unlocks the framerate of **Nioh: Complete
Edition** while keeping gameplay and animation speed stable as the actual
framerate changes.

The plugin is a Windows x64 DLL. The release package runs on Linux through
Proton and includes the required ASI loader.

## Status

- Current version: 1.6.2.
- Validated executable: Steam Nioh ProductVersion 1.24.8.0, dated 2022-08-25.
- Validated executable SHA-256:
  `56006af3fc0945248aa7a2e33fd95d4e510f1dbe3395eb3644dae3c2806377f6`
- Validated on Linux/Proton at 130 FPS and while changing external framerate
  caps during gameplay.
- Player and enemy motion, grass and bush wind, the Amrita Gauge pulse, water,
  and cloud animation retain their original wall-clock behavior above 60 FPS.
- Normal and aiming camera sensitivity, menu navigation, and firearm input are
  also normalized without changing their original 60 FPS behavior.
- Horizontally scrolling overflow text retains its original wall-clock speed.
- The original 30 FPS profiles remain capped and use their stock timing.

Other x64 `nioh.exe` builds are not rejected by version number. Before its
first write, Nioh1Fix must uniquely locate every required code path through
relocation-aware signatures and validate the derived globals, call targets,
input layout, and frame-profile table. A missing, changed, or ambiguous path
leaves the executable untouched. This can accommodate a relinked build whose
logic is unchanged, but it cannot infer a replacement for code that changed.

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
```

- `Enabled = false` prevents all Nioh1Fix memory patches.
- Control the actual framerate with an external limiter or leave it uncapped.
- The validated 120 FPS engine profile used during startup is an internal
  implementation detail and does not limit the measured presentation rate.

The timing corrections measure presentation intervals with
`QueryPerformanceCounter`. They therefore support arbitrary and changing
framerates rather than assuming 120 or 130 FPS. Separate verified hooks
normalize motion components, vegetation wind, SCL interface animation,
statistical-ocean water, three cloud systems, normal and aiming camera input,
overflow-text movement, and the transient/repeat input cadence used by menus.

## Architecture

The Windows runtime is split by responsibility across files under `src/`.
Pure profile and timing calculations live in `core.hpp`; executable signatures
are declarative data in `signatures.hpp`; shared runtime contracts are in
`runtime.hpp`. Platform access, timing callbacks, generic hook construction,
patch installation, monitoring, and process startup each have dedicated
translation units. Resolved addresses exist only in the current process and
are never written to configuration files.

The 150-line source-file limit is a decomposition guideline. When a readable
extension would exceed it, the relevant responsibility should move into a new
focused file; code should not be compressed simply to remain under the limit.

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

The package script creates `dist/Nioh1Fix-1.6.2.zip`. It downloads Ultimate
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

This reports PE metadata, the SHA-256 digest, frame-profile table matches, and
whether it is the exactly validated build. Runtime signature compatibility can
only be established after the game's protected code is available in memory.
The check does not modify the executable.

## Disable or Uninstall

To disable all patches without removing files:

```ini
[Framerate]
Enabled = false
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
