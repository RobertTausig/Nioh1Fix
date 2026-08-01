# Nioh1Fix Agent Handoff

## Goal and Current State

Nioh1Fix unlocks Nioh: Complete Edition on Linux/Proton and compensates
Katana Engine gameplay timing dynamically. Version 1.6.1 retains the validated
1.6.0 timing implementation and no longer exposes the internal 120 FPS startup
target as user configuration. The implementation has been validated at 130
FPS and while changing external framerate caps during gameplay. Player and
ordinary enemy animation, grass and bush wind, the Amrita Gauge pulse, water,
cloud movement, normal and aiming camera sensitivity, menu navigation, and
firearm input are validated.

Do not replace the working implementation with an earlier experimental
approach without new evidence.

## User Constraints

- Never stage or unstage files. Do not run `git add`, `git restore --staged`,
  commits, or other index-changing commands.
- Make normal working-tree edits and let Git show them.
- Do not create or modify per-game MangoHud configuration. The user manages
  FPS caps through their normal MangoHud setup.
- Do not launch the game automatically.
- Before installing a build, confirm that no Nioh process is running.
- Keep operations conservative. Previous overly broad operations caused editor
  instability.

## Paths

- Repository:
  `$HOME/Repos/Personal Repos/Nioh1Fix`
- Game:
  `/mnt/ssd/SteamLibrary/steamapps/common/Nioh`
- Runtime log:
  `/mnt/ssd/SteamLibrary/steamapps/common/Nioh/Nioh1Fix.log`
- Persistent analyzed executable:
  `$HOME/.local/share/Nioh1Fix/analysis/nioh.exe.unpacked.exe`

Keep the persistent analysis copy available across reboots. Never modify the
installed `nioh.exe`.

## Supported Executable

- Filename: `nioh.exe`
- Steam version: v1.24.07
- PE timestamp: `0x6307ABD5`
- Image size: `0x0306E000`
- SHA-256:
  `56006af3fc0945248aa7a2e33fd95d4e510f1dbe3395eb3644dae3c2806377f6`

Unsupported PE metadata is rejected before patching.

## Working Timing Model

The frame-profile table at RVA `0x017AA8D8` contains:

```text
60, 1, 1
30, 1, 2
30, 1, 2
60, 1, 2
```

Rows 0 and 3 are changed to the fixed internal target of 120. Rows 1 and 2 stay
at 30. Actual presentation timing remains dynamically measured.

The working unlock has three parts:

1. Raise the two gameplay frame-profile targets.
2. Disable the post-Present QPC limiter at RVA `0x00A9C2D0`.
3. Replace the Present dispatch at RVA `0x002B220C` with a helper that uses
   `SyncInterval = 0` and `DXGI_PRESENT_DO_NOT_WAIT`.

The direct target-FPS accessor at RVA `0x00E7D150` controls gameplay and
animation speed inversely. For gameplay profiles it must return:

```text
2 * measured presentation FPS
```

The Present helper records consecutive `QueryPerformanceCounter` timestamps.
`GetGameplayReferenceFps()` reads the most recent interval and returns twice
the resulting FPS, clamped to 30 through 2000. Before the first interval, it
returns `2 * 120`. Profiles 1 and 2 return the stock 30.

This empirical factor of two is required. Returning measured FPS improved but
did not fully correct speed. Returning 60 made animation much faster.

Separate unique-signature hooks scale three motion-component paths, normal
camera input, firearm/bow aiming input, grass/bush wind animation, the active
SCL interface animator, statistical-ocean water, and three cloud systems by
the measured presentation interval. A 60 Hz cadence gate clears only transient
and repeat input masks on skipped samples; the original input update still
runs every render frame.

Normal aggressive enemies confirmed correct idle, locomotion, blocking, and
attack animation timing. Passive tutorial enemies had been mistaken for a
remaining pathfinding-speed issue. Do not add a broad enemy or AI update hook
without new evidence; previous experiments regressed firearm and menu input.

## Important RVAs

- Frame profile table: `0x017AA8D8`
- Active profile index: `0x01BB01E8`
- Direct gameplay FPS accessor: `0x00E7D150`
- Reciprocal target helper: `0x00E7D170`
- Post-Present limiter: `0x00A9C2D0`
- Present dispatch: `0x002B220C`
- Original Present sync load: `0x002B2231`
- Frame controller: `0x019301D0`
- Normal camera scale block: `0x00853457`
- Aiming camera scale block: `0x0085E429`
- Grass and bush wind scale block: `0x0099105C`
- SCL interface-animation scale block: `0x0053370A`
- Statistical-ocean update: `0x003A3440`
- Cloud-plane update: `0x003A9150`
- Cloud-circle update: `0x003ABAB0`
- Cloud-particle update: `0x003AF180`
- Input cadence call: `0x00FABFA0`

Runtime patching must continue using unique full signatures. Do not patch bare
RVAs without signature and PE verification.

## Failed Experiments

- Profile values alone: profile reached 120, presentation stayed at 60.
- Sync interval and non-blocking Present alone: presentation stayed at 60.
- Shortening frame-ready waits: no FPS change.
- Disabling the Katana frame pacer: no FPS change.
- Bypassing worker consumer waits and the four-frame throttle: changed
  simulation speed but did not initially raise presentation. This was unsafe
  and is not in the final code.
- Hooking the producer for world offset `0x18322C`: ineffective for visible
  animation; diagnostics showed a seconds-based delta.
- The first trampoline for that hook clobbered `RAX`, which held the original
  stack pointer, and caused a startup crash. The hook was later removed.
- Returning a fixed 60 from `0x00E7D150`: compensation direction was wrong and
  animation became much faster.
- Scaling the fixed one-layout-frame branch of `CAnimatorBase@scl@ktgl@@` at
  RVA `0x0056D77C` did not affect the Amrita Gauge pulse. The active
  seconds-based branch at RVA `0x0053370A` is the validated fix.

See `docs/research.md` and Git history for the detailed sequence.

## Build and Test

Native tests:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Windows cross-build:

```bash
cmake --build build-windows -j2
```

If reconfiguration is needed, point `CMAKE_CXX_COMPILER` at an LLVM-MinGW
`x86_64-w64-mingw32-clang++` and set `CMAKE_SYSTEM_NAME=Windows`.

Before packaging:

```bash
git diff --check
```

Do not use Git commands that alter the index.

Package:

```bash
scripts/package.sh build-windows dist
```

The version in `CMakeLists.txt`, `scripts/package.sh`, and the startup log in
`src/dllmain.cpp` must remain synchronized.

Install only while the game is stopped:

```bash
unzip -o dist/Nioh1Fix-X.Y.Z.zip \
  -d "/mnt/ssd/SteamLibrary/steamapps/common/Nioh"
```

Do not launch Nioh. Ask the user to run it, exit, and report observed FPS and
speed, then inspect `Nioh1Fix.log`.

## Key Files

- `src/core.hpp`: pure frame-profile and timing-scale logic.
- `src/runtime.hpp`: shared runtime contracts, state, and constants.
- `src/signatures.hpp`: declarative full signatures and timing-hook specs.
- `src/platform.cpp`: logging, INI access, PE inspection, search, and writes.
- `src/timing.cpp`: presentation measurement, callbacks, input, and diagnostics.
- `src/hooks.cpp`: shared near-memory allocation and block-trampoline builder.
- `src/patches.cpp`: motion, input, limiter, accessor, and Present patching.
- `src/monitor.cpp`: patch installation state machine and integrity checks.
- `src/dllmain.cpp`: supported-process validation and DLL entry point only.
- `tests/frame_profile_tests.cpp`: native tests for profile behavior.
- `tests/timing_scale_tests.cpp`: native tests for timing-scale behavior.
- `Nioh1Fix.ini`: runtime enable switch.
- `docs/research.md`: reverse-engineering record.
- `scripts/package.sh`: Linux packaging and pinned ASI loader.
- `scripts/package.ps1`: Windows packaging.

Keep the files under `src/` at or below 150 lines each. The native tests
cover portable profile and timing math. Hook correctness still requires runtime
validation on the supported executable.
