# Framerate research

## Examined build

- Product: Nioh: Complete Edition
- Game version: v1.24.07
- PE timestamp: `0x6307ABD5`
- PE image size: `0x0306E000`
- Packed executable SHA-256:
  `56006af3fc0945248aa7a2e33fd95d4e510f1dbe3395eb3644dae3c2806377f6`

Analysis was performed against a temporary copy. The installed executable was
not unpacked or modified.

## Timing path

The relevant Katana Engine path is a four-row frame profile table:

| Row | Target float | Flags |
| --- | ---: | --- |
| 0 | 60.0 | 1, 1 |
| 1 | 30.0 | 1, 2 |
| 2 | 30.0 | 1, 2 |
| 3 | 60.0 | 1, 2 |

The table is unique in the supported executable. A selected row feeds:

- a helper that returns the target FPS;
- a helper that computes `1.0 / target FPS`;
- the main update loop's normalized delta;
- the QPC-based frame deadline and wait path.

This is materially safer than removing the wait call alone. Raising the two
60.0 values changes the limiter cadence and the engine's corresponding
timestep source together. The two 30.0 rows are deliberately preserved.

Because the ASI loader runs while SteamStub is still initializing the process,
the plugin monitors the verified table for 30 seconds. If SteamStub restores
the exact original table after the initial write, the plugin logs the event and
reapplies the configured target. Any other change is treated as unexpected and
stops the monitor without writing.

Nioh also initializes a renderer field at offset `0x2F8C` to one and passes it
as `SyncInterval` to `IDXGISwapChain::Present`. The settings code can restore
that value, and Nioh exposes no VSync toggle. After SteamStub decrypts the code
section, the plugin finds the unique complete Present block and replaces the
six-byte sync-interval load with `xor edx, edx` and four NOP bytes. This removes
the separate presentation cap while the frame profile retains the configured
target.

Relevant RVAs in the supported build:

- Frame profile table: `0x017AA8D8`
- Return target profile float: `0x00E7D150`
- Compute reciprocal target: `0x00E7D170`
- Frame loop profile reads: `0x00A9D8AF` and `0x00A9DC77`
- QPC wait helper: `0x00A9C2D0`
- Wait call sites: `0x00A9DA39` and `0x00A9DE19`
- Present sync-interval load: `0x002B2231`

RVAs are documentation only. The plugin locates the full table signature,
requires exactly one match in initialized readable data, verifies the PE
timestamp and image size, and checks the complete original table immediately
before writing.

## Remaining runtime checks

Before a stable release, validate at 60, 120, and an intentionally uneven load:

- gameplay speed over a timed route;
- dodge, attack, and invulnerability frame behavior;
- enemy AI and projectile speed;
- cloth and ragdoll behavior;
- in-engine and video cutscenes;
- menus, loading, focus loss, and frame drops;
- Steam Overlay and Proton restart behavior.

Capture frame times and compare wall-clock completion time at 60 and 120 FPS.
