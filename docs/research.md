# Framerate Research

## Examined Build

- Product: Nioh: Complete Edition
- Game version: v1.24.07
- PE timestamp: `0x6307ABD5`
- PE image size: `0x0306E000`
- Packed executable SHA-256:
  `56006af3fc0945248aa7a2e33fd95d4e510f1dbe3395eb3644dae3c2806377f6`

Analysis uses the persistent unpacked copy at
`$HOME/.local/share/Nioh1Fix/analysis/nioh.exe.unpacked.exe`. The installed
executable was not modified.

## Frame Profiles

Katana Engine uses a four-row frame-profile table:

| Row | Stock target | Values |
| --- | ---: | --- |
| 0 | 60.0 | 1, 1 |
| 1 | 30.0 | 1, 2 |
| 2 | 30.0 | 1, 2 |
| 3 | 60.0 | 1, 2 |

Nioh1Fix changes rows 0 and 3 to its fixed internal target of 120. Rows 1 and
2 remain at 30. This target is not exposed as user configuration because the
actual presentation rate is measured dynamically.

The table alone does not unlock presentation. The two gameplay rows stayed
patched to 120 during testing while the game continued presenting at 60 FPS.

## Confirmed Limiter Path

Three independent changes are required:

1. Patch the two 60 FPS gameplay profiles to the internal 120 FPS target.
2. Disable the QPC-based limiter called immediately after each `Present`.
3. Replace the game's Present dispatch with `SyncInterval = 0` and
   `DXGI_PRESENT_DO_NOT_WAIT`.

The post-Present limiter at RVA `0x00A9C2D0` was the final effective 60 FPS
cap. Once disabled, the game reached an external MangoHud cap of 130 FPS in
menus and gameplay.

## Dynamic Gameplay Timing

Unlocking presentation exposed a second issue: gameplay and animation speed
depended on a direct target-FPS accessor at RVA `0x00E7D150`. This accessor has
roughly 190 static call sites in the supported executable.

Experiments established the direction of the dependency:

- Returning 120 while presenting at 130 left gameplay somewhat too fast.
- Returning the stock value 60 made gameplay approximately twice as fast.
- Returning the measured presentation FPS improved timing.
- Returning twice the measured presentation FPS produced stable wall-clock
  animation duration and remained correct while changing FPS caps at runtime.

For gameplay profiles, the final correction is:

```text
timing_divisor = 2 * measured_presentation_fps
```

Presentation intervals are measured once per frame with
`QueryPerformanceCounter`. The accessor uses the immediately preceding frame
interval and clamps the result to 30 through 2000. Before the first measured
interval, it returns `2 * 120` as a startup fallback.

The two 30 FPS profiles bypass dynamic compensation and return 30.

## Component Timing and Input

The dynamic gameplay divisor corrects the player and the verified shared
motion-component paths. Additional narrowly scoped corrections are required
for systems that consume per-render values directly:

- Three motion-component delta calls use the measured presentation scale.
- Normal gameplay camera axes are scaled at RVA `0x00853457`.
- Firearm and bow aiming-camera axes are scaled at RVA `0x0085E429`.
- The grass and bush wind phase is scaled at RVA `0x0099105C`.
- The active seconds-based SCL interface-animation branch is scaled at RVA
  `0x0053370A`; this corrects the Amrita Gauge pulse.
- The `CStatisticalOcean::Update` delta is scaled at RVA `0x003A3440`.
- Cloud movement deltas are scaled in `CCloudPlane::Update` at RVA
  `0x003A9150`, `CCloudCircle::Update` at RVA `0x003ABAB0`, and
  `CCloudParticleObject::Update` at RVA `0x003AF180`.
- Transient pressed, released, and repeat input state is exposed at the
  original 60 Hz cadence through the call at RVA `0x00FABFA0`.

Each location is found through a unique full signature. The RVAs above are
diagnostic references, not unverified patch targets. The timing scale is
bounded during startup, cap changes, jitter, and stalls; stock 30 FPS profiles
use a scale of one.

The input cadence gate calls the original input update every render frame.
Only transient and repeat masks are cleared on skipped cadence samples. This
preserves analog state and firearm trigger handling while preventing one D-pad
press from producing several menu transitions.

Testing initially suggested that enemy movement needed another pathfinding
hook. Those observations came from passive tutorial enemies. Normal aggressive
enemies later confirmed that idle, movement, blocking, and attack animation
timing are already corrected by the validated gameplay and motion-component
paths. No additional AI, pathfinding, physics, attack, or invulnerability hook
is installed.

## Runtime Patching

SteamStub decrypts code after the ASI starts. Nioh1Fix monitors for 30 seconds
at 250 ms intervals, locates full byte signatures in executable sections, and
requires unique matches before patching.

The frame-profile table is also monitored. If it returns to the exact original
state, the internal 120 FPS target is reapplied. Any unexpected table state stops
monitoring without another write.

Relevant RVAs for the supported build:

- Frame profile table: `0x017AA8D8`
- Active frame profile: `0x01BB01E8`
- Direct gameplay FPS accessor: `0x00E7D150`
- Reciprocal target helper: `0x00E7D170`
- Post-Present QPC limiter: `0x00A9C2D0`
- Present dispatch block: `0x002B220C`
- Original Present sync-interval load: `0x002B2231`
- Frame controller: `0x019301D0`
- SCL interface-animation scale: `0x0053370A`
- Statistical-ocean update: `0x003A3440`
- Cloud-plane update: `0x003A9150`
- Cloud-circle update: `0x003ABAB0`
- Cloud-particle update: `0x003AF180`

RVAs are documentation and diagnostics. Runtime writes use verified full
signatures and supported PE metadata.

## Rejected Approaches

- Patching only the frame-profile table did not exceed 60 FPS.
- Forcing non-blocking Present did not exceed 60 FPS while the post-Present
  limiter remained active.
- Bypassing main and worker synchronization produced faster simulation without
  increasing presentation and is not part of the final implementation.
- Scaling the value at world offset `0x18322C` was ineffective for visible
  animation. Diagnostics showed that value was already a seconds-based delta.
- Returning a fixed stock 60 from the direct FPS accessor applied compensation
  in the wrong direction and increased animation speed.
- Experimental broad enemy-update hooks did not improve the tutorial
  observation and caused firearm or menu regressions. They are not part of the
  validated implementation.
- Scaling the fixed one-layout-frame-per-render branch of
  `CAnimatorBase@scl@ktgl@@` at RVA `0x0056D77C` did not affect the Amrita
  Gauge pulse. That experiment was removed; the active seconds-based branch at
  RVA `0x0053370A` was later identified and validated.

## Validation

Validated on Linux with Proton:

- 130 FPS in menus and gameplay.
- Runtime changes between external FPS caps.
- Stable player, ordinary enemy, grass, bush, Amrita Gauge pulse, water, and
  cloud animation duration in wall-clock seconds.
- Stable normal and firearm/bow aiming-camera sensitivity.
- One menu step per D-pad press, normal hold-to-repeat behavior, and working
  firearm input.
- Clean startup after the final accessor-based implementation.

Areas that still warrant broader regression testing include cutscenes, physics,
invulnerability timing, focus loss, and sustained uneven frame times.
