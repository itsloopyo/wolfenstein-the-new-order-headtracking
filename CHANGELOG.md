# Changelog

## [Unreleased]

### Added
- Support for the Xbox Game Pass / Microsoft Store copy of the game. It is a
  different, later build than the Steam one, so it gets its own set of
  addresses; the installer and the dev deploy now find it too.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed
- Settings move to `CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. A nav-cluster code and its chord letter from `HeadTracking.ini` (`ToggleKey` and `ChordToggleKey`, for example) become one list, and `CycleModeKey` becomes `CycleTrackingModeKey`.
- The other settings keep their meaning under the fleet's names: `[Rotation] LocalSmoothing` and `RemoteSmoothing` move to `[Smoothing]`, `[Position] Enabled` becomes the startup tracking mode `RotationEnabled` / `PositionEnabled`, `LimitX`, `LimitZ` and `LimitZBack` become `PositionLimitX`, `PositionLimitZ` and `PositionLimitZBack`, and `LimitY`, which bounded the lean down as well as up, becomes both `PositionLimitY` and `PositionLimitYDown`.
- Cycling the tracking mode (`Page Up` / `Ctrl+Shift+G`) and switching the yaw mode (`Page Down` / `Ctrl+Shift+H`) now save the new mode to `CameraUnlock.ini`, so the game starts in it next time. Turning head tracking on or off with `End` still changes the current session only; whether it starts on is `EnableOnStartup`.
- A value in `CameraUnlock.ini` the mod cannot use keeps its default and the log names the line; nothing is clamped. The position limits take any value from 0 to 10 metres, where `HeadTracking.ini` pulled anything above 0.5 back to 0.5, and `UdpPort` takes 1 to 65535, where `HeadTracking.ini` took 1024 to 65535.
- Since the dev build of 2026-09-13 (c37f0f4), `HeadTracking.ini`'s `AdsMode`, `AdsModeKey` and `ChordAdsModeKey` are no longer read and `Insert` / `Ctrl+Shift+U` no longer cycle what tracking does down the sights: head tracking stays on while you aim and only the lean eases out (1e5ffad).
- `uninstall.cmd` keeps `CameraUnlock.ini` and `HeadTracking.ini`; it used to delete `HeadTracking.ini`. Nothing installs, seeds or ships either file.

## [0.0.0] - 2026-09-02

### Added
- Initial release.
- Added startup centring of the game window on its monitor, for a game running
  windowed that has not centred itself.
