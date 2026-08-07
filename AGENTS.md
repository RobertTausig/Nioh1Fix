# Nioh1Fix Agent Handoff

## Goal and Current State

Nioh1Fix unlocks Nioh: Complete Edition on Linux/Proton and compensates
Katana Engine gameplay timing dynamically. The current version retains the
validated timing implementation and no longer exposes the internal 120 FPS
startup target as user configuration. The implementation has been validated
across arbitrary framerates and while changing external caps during gameplay.
Player and ordinary enemy animation, grass and bush wind, the Amrita Gauge
pulse, water, cloud movement, normal and aiming camera sensitivity, menu
navigation, firearm input, and directional lock-on target switching are
validated.
Horizontal overflow-text scrolling is normalized.

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
- For GitHub issue work, read `docs/issue-investigation.md`.

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

## Validated Executable

- Filename: `nioh.exe`
- Steam ProductVersion: 1.24.8.0
- PE timestamp: `0x6307ABD5`
- Image size: `0x0306E000`
- SHA-256:
  `56006af3fc0945248aa7a2e33fd95d4e510f1dbe3395eb3644dae3c2806377f6`

The timestamp and image size identify this validated build for diagnostics.
Other x64 `nioh.exe` builds are not rejected by metadata alone. Before any
write, every required relocation-aware signature and all derived targets and
layouts must validate uniquely; otherwise no changes are made.

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
camera rotation coefficients, firearm/bow aiming input, grass/bush wind
animation, the active SCL interface animator, statistical-ocean water, and
three cloud systems by the measured presentation interval. The raw normal-
camera axes remain unscaled because lock-on target switching compares them
with a fixed input threshold. A 60 Hz cadence gate runs the original input
update on accepted samples and clears transient and repeat masks on skipped
invocations.

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
- Overflow-text scroll update: `0x0056C1B0`
- Input cadence call: `0x00FABFA0`

These RVAs are documentation and diagnostic references only. Every current
and future runtime address used for a read, write, call, or hook must be found
through a unique relocation-aware full signature. An address derived from a
signature match is allowed only when its displacement and target layout are
also validated. Only audited address displacements may be wildcarded. Never
use a bare RVA as a runtime patch input or compatibility fallback. Add every
new signature and derived target to the complete fail-closed compatibility
plan that validates all locations before the first write.

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
- Calling the original input update on every cadence-gate invocation regressed
  menu navigation and did not fix lock-on switching.
- Scaling the raw normal-camera axes weakened lock-on target switching because
  the engine compares those same values with a fixed threshold. Scale the
  camera rotation coefficients instead.
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

Run the GitHub Actions workflow locally with the distribution package for
`act` and a local artifact server:

```bash
act_artifacts="$(mktemp -d)"
act push \
  -P ubuntu-latest=catthehacker/ubuntu:act-latest \
  --artifact-server-path "${act_artifacts}"
```

Normal pushes and pull requests build the native Linux tests, run them, and
cross-build and package the Windows plugin. A pushed `vX.Y.Z` tag must exactly
match the version in `VERSION`; after both build jobs pass, the workflow
creates or updates the corresponding GitHub Release with the tested ZIP.

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

`VERSION` is the release-version source of truth. CMake passes that version to
the DLL startup log, and both package scripts use it for archive names.

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
- `src/signatures.hpp`: relocation-aware signatures and hook specifications.
- `src/compatibility.cpp`: complete fail-closed compatibility-plan resolution.
- `src/platform.cpp`: logging, INI access, PE inspection, search, and writes.
- `src/timing.cpp`: presentation measurement, callbacks, input, and diagnostics.
- `src/hooks.cpp`: shared near-memory allocation and block-trampoline builder.
- `src/patches.cpp`: motion, input, limiter, accessor, and Present patching.
- `src/monitor.cpp`: patch installation state machine and integrity checks.
- `src/dllmain.cpp`: supported-process validation and DLL entry point only.
- `tests/frame_profile_tests.cpp`: native tests for profile behavior.
- `tests/timing_scale_tests.cpp`: native tests for timing-scale behavior.
- `VERSION`: release-version source of truth.
- `Nioh1Fix.ini`: runtime enable switch.
- `docs/research.md`: reverse-engineering record.
- `scripts/package.sh`: Linux packaging and pinned ASI loader.
- `scripts/package.ps1`: Windows packaging.

Treat 150 lines as a decomposition threshold, not a reason to compress code.
If a clear, normally formatted extension would take a file beyond that size,
split the file by responsibility and add another focused source or header.
Do not combine statements, remove useful whitespace, shorten names, or weaken
the structure merely to satisfy the limit. The native tests cover portable
profile, masked-signature, and timing math. Hook correctness still requires
runtime validation on the validated executable.
