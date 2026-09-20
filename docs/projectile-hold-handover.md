# Projectile-hold animation handover

## Scope

This document covers only the enemy-archer arrow-hold timing issue. Projectile
velocity, reach, Shot posture cadence, and Shot lifetime cadence are separate
features that were already validated before this investigation.

## Confirmed symptom and measurements

- The user observed enemy archers holding a loaded arrow for less time at
  135 Hz than at 45 Hz.
- The release diagnostic measures successive ordinary-arrow releases from one
  owner. It does not directly timestamp the moment at which the arrow first
  becomes visible in the bow.
- In the baseline run, five 135 Hz intervals were within 1.319--1.363 seconds
  and five 45 Hz intervals were within 1.685--1.692 seconds.
- In the most recent completed run, the same-owner 135 Hz dataset was
  `17.241, 1.652, 1.392, 1.397, 1.369, 1.372` seconds. After removing the one
  farthest outlier, its median was 1.392 seconds.
- The same run's 45 Hz dataset was
  `0.790, 1.689, 1.670, 1.688, 1.688, 1.707, 1.670, 1.707` seconds. After
  removing the one farthest outlier, its median was 1.688 seconds.
- The latest median gap was therefore 0.296 seconds. This is closer than the
  baseline gap, which was necessarily at least 0.322 seconds from the recorded
  ranges, and supports the user's impression of a modest visual improvement.
- The required analysis rule is: for each Hz-mode dataset, discard exactly one
  farthest timing outlier and compare the medians of the remaining samples. Do
  not discard one sample per group of five.
- The tracked action's float at `+0x24` was approximately 0.21--0.23 per call
  at 135 Hz and 0.66--0.67 per call at 45 Hz.
- The tracked action's current frame is the float at `+0x28`. Its virtual frame
  getter is at RVA `0x007300D0` and returns that field.
- The tracked action has vtable RVA `0x011A3530`; RTTI identifies it as
  `CActModuleActionMotNode`.
- At steady 135 Hz, the recurring action-frame reset endpoints before a release
  were approximately `44 -> 0`, `28 -> 0`, and `12 -> 0`. The `44 -> 0`
  reset occurred about 0.85 seconds before release.
- At steady 45 Hz, the recurring endpoints were approximately `44 -> 0`,
  `50 -> 0`, `18 -> 0`, and `12 -> 0`. The `44 -> 0` reset occurred about
  1.29 seconds before release.
- Therefore the two caps selected observably different action-frame sequences;
  the difference was not merely a stationary-frame plateau.

## Confirmed executable path

- RVA `0x00718CE0` is a virtual action/motion update with arguments
  `(action, motion_source)`.
- It calls RVA `0x0071D810` with `motion_source`.
- It then loads the float at `motion_source + 0x60`, compares it with
  `motion_source + 0xF0`, sets the action completion bit when appropriate, and
  copies the float to `action + 0x28` at RVA `0x00718D24`.
- RVA `0x0071D810` does not write `motion_source + 0x60` in the inspected
  function body. The producer of that field has not been identified.
- The relocation-aware signature added as `kArcherMotionUpdate` matches exactly
  once in the validated executable.

## Confirmed negative results

- There was no stationary `action + 0x28` plateau that explained the cap
  difference.
- Temporarily changing `action + 0x24` around the action-event update did not
  change the measured 135/45 release timing. The value consumed at
  `action + 0x28` comes from the separate motion source described above.
- Gating the complete action-event updater to 30 Hz was not safe. Most loaded
  arrows then disappeared instead of firing because event updates were skipped.
- A non-skipping `action + 0x24` experiment restored most firing, but one of
  roughly ten observed loaded arrows still disappeared. It did not establish a
  hold-timing fix.
- The final phase-only `action + 0x24` experiment produced no disappearing
  arrows in the user's observed sample, but measured release intervals remained
  about 1.31--1.33 seconds at 135 Hz and 1.67--1.69 seconds at 45 Hz. The user's
  perception of equal timing in that run was not supported by the log.
- Scaling positive changes of `motion_source + 0x60` after the motion-node
  helper ran was ineffective. The hook activated and changed hundreds of
  samples, but release intervals remained frame-rate dependent, indicating
  that the adjusted value was not the controlling state.
- A later attempt wrapped the active and pending action-event calls and tried
  to learn the event definition responsible for Shot construction. Its runtime
  counters remained `0/0/0/0`, proving that Shot construction did not occur
  under either wrapped context. The release intervals from that run produced
  the 1.392/1.688-second medians above, so that modest improvement cannot be
  attributed to the inactive event hook. The hook is no longer compiled.
- Ordinary enemy-arrow Shot construction occurred at release, not when the
  loaded arrow became visible. Consequently Shot-construction-to-release timing
  reported approximately zero and cannot measure the visible hold.
- A direct 8 KiB integer scan of the archer owner produced no useful candidate
  counter correlated with the observed action resets. This result does not
  prove that no relevant state exists elsewhere.

## Current implementation state

- `src/archer_action_timing.cpp` now wraps every action-event update only to
  preserve Shot/action correlation and diagnostics. It calls the original on
  every invocation; it performs no cadence gating and no `action + 0x24`
  modification.
- `src/archer_motion_timing.cpp` is the newly installed candidate solution. It:
  - applies only to the action pointer learned from a released ordinary arrow;
  - detects the observed `40--46` to below-5 motion-frame transition;
  - cadences the complete RVA `0x00718CE0` motion-node update to 60 Hz during
    that phase,
    skipping excess calls above 60 Hz and issuing bounded catch-up steps below
    60 Hz;
  - hooks the function entry and calls an original-function trampoline, instead
    of editing `motion_source + 0x60` after the original has run; and
  - ends its held-phase state when the correlated arrow release is observed.
- Static inspection has not identified the writer of `motion_source + 0x60`.
  Consequently, the below-60 catch-up calls are an implementation attempt, not
  proof that the source frame will advance multiple times; runtime timing is
  still decisive.
- Because the tracked action is learned from a release, the first arrow seen
  after process startup is not modified by this candidate implementation.
- Stock 30 FPS profiles bypass this candidate implementation.
- The motion-update cadence implementation was built, packaged as
  `dist/Nioh1Fix-1.9.0.zip`, and installed in the Nioh directory after checking
  that Nioh was stopped.
- The installed `Nioh1Fix.asi` matched the build SHA-256
  `f4cd029cd898ce12d96885481b4470d9a6c43c963360281c9756a5ad1f0d9be0`.
- Native tests passed and the Windows cross-build passed. Those tests do not
  exercise the runtime hook or prove the hold timing.
- The game has not been run with this newly installed motion-update cadence
  implementation. Its effect on hold duration and arrow firing is unknown.

## Required next validation

- Have the user run Nioh; do not launch it automatically.
- Obtain about seven ordinary enemy-arrow releases at 135 Hz and seven at
  45 Hz, then have the user exit the game. The first release teaches the hook
  which action pointer to track and is not modified.
- Inspect `Nioh1Fix.log` for startup/install success, release intervals, reset
  timelines, the `archer_motion` counters, and any loaded arrows that
  disappeared instead of firing. Those counters are
  `calls/transitions/starts/original_steps/skipped_calls/held`.
- For comparison, discard exactly one farthest timing outlier from each Hz-mode
  dataset, especially because a cap switch can occur while an arrow is held.
- Do not describe the issue as fixed unless both the log timing and firing
  behavior support that conclusion.

## Repository state warning

The repository was already in a mixed staged/unstaged diagnostic state during
this investigation. Several related files have both index and working-tree
changes; the removed event-hook files are still represented in the index. Do
not stage, unstage, reset, or otherwise normalize the index; preserve all
existing work.
