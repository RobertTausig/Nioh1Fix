# Nioh1Fix

Nioh1Fix unlocks the framerate in **Nioh: Complete Edition** and lets it support arbitrary frametimes while keeping the games intended animation durations. It is an ASI plugin for the
64-bit Windows game and works on both Windows and Linux through Proton.

It corrects the framerate-dependent behavior of:

- player and enemy animation;
- grass, bushes, water, clouds, and the Amrita Gauge;
- normal and aiming camera sensitivity;
- menu navigation and horizontally scrolling menu text;
- firearm input.

Not only is the original behaviour corrected for framerates over 60 Hz, it is also corrected for those under 60 Hz (So caps at eg. 46 Hz, 77 Hz, or 135 Hz will all work as expected from modern games and can therefore be used with VRR displays).

The mod has been tested with the Steam 1.24.8.0 executable.

This mod is developed and used by me on Linux, so the Windows version is untested as of version `1.7.0`.

## Installation and use

1. Download the
   [latest GitHub release](https://github.com/RobertTausig/Nioh1Fix/releases/latest).
2. Extract all files into the game directory beside `nioh.exe`.
3. In Nioh, select the normal 60 FPS mode.
4. Start the game. You may leave the framerate uncapped or use an external
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

## Thanks

- [Lyall](https://github.com/Lyall) for Katana Engine fixes that provided
  useful ASI and Proton packaging examples.
- [ThirteenAG](https://github.com/ThirteenAG) and contributors for
  [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).

Nioh1Fix is not affiliated with Koei Tecmo or Team Ninja.
