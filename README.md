# Nioh1Fix

Nioh1Fix unlocks the framerate in **Nioh: Complete Edition** and lets it support arbitrary frametimes while keeping the games intended animation durations. It is an ASI plugin for the
64-bit Windows game and works on both Windows and Linux through Proton.

It corrects the framerate-dependent behavior of:

- player and enemy animation;
- grass, bushes, water, clouds, and the Amrita Gauge;
- normal and aiming camera sensitivity, including directional lock-on target
  switching;
- menu navigation and horizontally scrolling menu text;
- gameplay input timing, including firearm input and distinction
  between short-press evasion and hold-to-run.


The mod has been tested with the Steam 1.24.8.0 executable. This mod is developed and used by me on Linux.

> [!WARNING]
> **Windows build status:** User feedback has confirmed that the Windows build
> is not yet ready for use. Although the framerate unlock works, animations
> currently play too quickly. For now, development will focus on the
> Linux/Proton version. A fully working Windows version is planned for release
> `2.0.0`.

## Installation and use

1. Download the
   [latest GitHub release](https://github.com/RobertTausig/Nioh1Fix/releases/latest).
2. Extract all files into the game directory beside `nioh.exe`.
3. In Nioh, select the normal 60 FPS mode.
4. Start the game. You _must not_ leave the framerate uncapped. Use an external
   limiter such as MangoHud or your graphics-driver limiter.

Linux/Proton users must also add this Steam launch option:

```text
WINEDLLOVERRIDES="version=n,b" %command%
```

Windows users do not need a launch option. The included `version.dll` loads
the plugin on both platforms.

The mod is enabled by default. To disable it, edit `Nioh1Fix.ini`:

```ini
[Framerate]
Enabled = false
```

Restart the game after changing the setting. If something does not work,
close the game and check `Nioh1Fix.log` in the game directory.

To uninstall, remove `Nioh1Fix.asi`, `Nioh1Fix.ini`, and `Nioh1Fix.log`.
Remove `version.dll` and the Proton launch option only if no other mod uses
that ASI loader.

## Development builds

The native tests and Windows plugin use the same verified LLVM-MinGW release.
The versions and checksums for Clang, LLVM-MinGW, and CMake are maintained
together in `cmake/toolchain-versions.env`. Unmodified copies of the pinned
toolchain and ASI loader archives are retained in `third_party/archives`. The
build scripts prefer those copies, verify every archive before use, and retain
the upstream download URLs as fallbacks.

On Linux, prepare the pinned toolchain and run the native tests with:

```bash
toolchain_directory="$(scripts/ensure-llvm-mingw.sh)"
export PATH="${toolchain_directory}/bin:${PATH}"
scripts/cmake.sh --preset linux-clang
scripts/cmake.sh --build --preset linux-clang
scripts/ctest.sh --preset linux-clang
```

The Windows cross-build and package command is:

```bash
scripts/build-linux.sh
```

## FAQ

### Which framerates are supported? Nioh 2 & 3 have a framerate lock of 120 Hz on PC.

Arbitrary framerates are supported. I tested up until 135 Hz, but that is just because my hardware can't reach higher framerates consistently.

### Can I also use the mod for framerates _under_ 60 Hz?

Yes. In unmodded Nioh's 60 FPS mode - if your framerates were under 60 Hz - timing-dependent animation and gameplay speed ran too slowly. Nioh1Fix compensates for that difference.
You could lock your framerate to e.g. 46 Hz and everything will work as expected.

### Why do I need to use an external framerate limiter?

The mod will automatically recognize the current framerate and change the animation durations to fit the wall-clock-intention. This process takes a split second. If your framerate fluctuates freely, the mod tries to adapt constantly, which results in a very stuttery presentation.

### Can I change the framerate limit during gameplay?

Yes, you can change framerate limits freely during gameplay (E.g. toggling between a 77 Hz and 102 Hz limit). You will notice a split second of automatic adaption.

### Does this mod work in online play?

This is untested, so I don't know. Do note that - while I find it unlikely - Team Ninjas server may identify you as cheater. I'll update this point as soon as it is tested.

## Thanks

- [Lyall](https://github.com/Lyall) for Katana Engine fixes that provided
  useful ASI and Proton packaging examples.
- [ThirteenAG](https://github.com/ThirteenAG) and contributors for
  [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).

Nioh1Fix is not affiliated with Koei Tecmo or Team Ninja.
