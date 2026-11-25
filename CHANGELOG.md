# Changelog

All notable changes for players are listed here, newest first. Each GitHub release copies its section.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [1.0.2] - 2026-11-25

### Fixed

- Changing the head in the player creation screen no longer freezes the game
  ([#1](https://github.com/KevinL50-sysadmin/F1-05-PC/issues/1)).

## [1.0.1] - 2026-11-22

### Changed

- The installer's Start menu and desktop shortcuts, and its entry in Settings > Apps, are now called "F1 05"
  instead of "F1 05 PC". Updating from 1.0.0 removes the old "F1 05 PC" shortcuts. The install folder stays the same.

## [1.0.0] - 2026-11-22

The first public release. It contains no game data: on the first start you choose your own copy
of Formula One 05 (PS2, European PAL version, SCES-53033) as an `.iso` file.

### Added

- The original PS2 game, as it is on the console, running natively on Windows: a static recompilation of the
  game's code (PS2Recomp), not an emulator. No PC extras: no new options in the game's menus, no photo mode, no
  online play.
- First-run setup from your own disc: the installer, or the game the first time it opens, finds or asks for your
  `.iso`, checks it is Formula One 05 PAL (SCES-53033, the Redump "Europe, Australia (v1.02)" dump) and copies its
  files to `data\` (about 3.5 GB) with a progress bar that can be cancelled. The ISO is only read, never changed.
  `F1-05.exe --extraer-iso "path\game.iso"` and `--comprobar-iso` do the same from the command line, with exit
  codes.
- GPU rendering by default (paraLLEl-GS on Vulkan, `gs = gpu`), with the software renderer (`gs = cpu`) as a
  fallback when Vulkan or `pgs_puente.dll` is missing. The graphics already prepared for your card are kept in
  `cache\`, so there is less stutter the next time.
- PC settings in `config\settings.ini` (and on the game's page in RecompLauncher): window, borderless or full
  screen, window size, aspect ratio (4:3, 16:9, console pixels), scaling filter (nearest, bilinear, sharp pixels,
  AMD FSR 1, CRT), supersampling and double resolution, V-Sync, FPS counter, volume, audio latency and mono, and
  vibration strength. F11 switches between window and full screen, F10 changes the aspect ratio.
- Controller, keyboard and steering wheel support, with vibration; buttons and keys are set in
  `config\controls.ini`.
- Memory cards in `saves\mc0` and `saves\mc1`.
- Crash reports: when the game fails it closes with an exit code other than 0 and writes what happened to
  `logs\F1-05.log` (the previous session stays in `F1-05.anterior.log`), so RecompLauncher can show the error and
  offer to send the report with the log attached. A watchdog also logs where the game was stuck if it freezes.
- Portable `.zip` (everything is saved inside its folder) and a Windows installer (saves and options in
  `Documents\My Games\F1-05`, log and cache in `%LOCALAPPDATA%\F1-05`), with `recomplauncher.json` for
  RecompLauncher.

### Known issues

- With 20 cars at the start and in the pit lane the frame rate can dip briefly below 50; the first race after
  installing also compiles the graphics for your card once (the cache in `cache\` avoids it next time).
- With the software renderer (`gs = cpu`) the pause menu sometimes does not appear: the picture stays on the last
  frame of the race. It does not happen with the GPU renderer, the default.
