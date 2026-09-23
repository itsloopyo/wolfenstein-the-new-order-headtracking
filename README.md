# Wolfenstein: The New Order Head Tracking

![Wolfenstein: The New Order running with this mod](https://raw.githubusercontent.com/itsloopyo/wolfenstein-the-new-order-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Wolfenstein: The New Order that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view, your mouse or controller keeps the aim
- **6DOF tracking** - yaw, pitch and roll, plus positional lean, peek and duck
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- A purchased copy of Wolfenstein: The New Order, on [Steam](https://store.steampowered.com/app/201810/) or Xbox Game Pass
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack/releases), or any app that sends the OpenTrack UDP protocol
- 64-bit Windows 10 or 11

The installer ZIP bundles the x64 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases), so you do not need to download it separately.

## Installation

### Standalone Installer

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/wolfenstein-the-new-order-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder yourself, either with an environment variable or as an argument:

```powershell
$env:WOLFENSTEIN_THE_NEW_ORDER_PATH = "D:\Games\Wolfenstein.The.New.Order"
.\install.cmd
```

```powershell
.\install.cmd "D:\Games\Wolfenstein.The.New.Order"
```

`install.cmd` installs into one copy of the game. If you own it on both Steam
and Xbox Game Pass it picks the Steam one, so run it a second time with the other
folder as the argument to cover both:

```powershell
.\install.cmd "C:\XboxGames\Wolfenstein- The New Order (PC)\Content"
```

### Manual Installation

The installer ZIP holds `plugins/WolfensteinTheNewOrderHeadTracking.asi`, `vendor/ultimate-asi-loader/` (the loader, its upstream LICENSE, and a README recording the version and checksum), `install.cmd`, `uninstall.cmd`, `launcher-manifest.json`, the shared game-detection scripts, `README.md`, `CHANGELOG.md`, `LICENSE`, `THIRD-PARTY-NOTICES.md` and `licenses/`.

To place the files by hand:

1. Copy `vendor/ultimate-asi-loader/dinput8.dll` into the game folder, next to `WolfNewOrder_x64.exe`. On Steam that is `<Steam library>\steamapps\common\Wolfenstein.The.New.Order\`; on Xbox Game Pass it is the package folder, `<drive>:\XboxGames\Wolfenstein- The New Order (PC)\Content\`. No rename is needed: that executable already imports `dinput8.dll`, so the loader is picked up under its own filename.
2. Copy `plugins/WolfensteinTheNewOrderHeadTracking.asi` into the same folder.
3. Launch the game.

Mod managers do not deploy this mod. Vortex ships no extension for Wolfenstein:
The New Order, so it cannot discover the game or deploy to it at all, and these
two files have to sit beside `WolfNewOrder_x64.exe` rather than in the mod
subfolder a manager deploys into. Mod Organizer 2 would need Root Builder. Use
`install.cmd`, or copy the two files by hand as above. There is no Nexus download
for the same reason.

## Setting Up OpenTrack

- **Input**: whichever tracker you are using
- **Output**: `UDP over network`
- **Output options**: host `127.0.0.1`, port `4242`
- Map yaw, pitch and roll, plus X, Y and Z if you want positional tracking
- Press **Start**, then launch the game

Centering is done in your tracker: OpenTrack's Center bind, your phone app's own center button, or SteamVR's reset.

### VR Headset Setup

1. Connect the headset over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on `UDP over network`, host `127.0.0.1`, port `4242`.

### Webcam Setup

1. Set OpenTrack's **Input** to `neuralnet tracker`. It needs no markers, no clip and no IR hardware, just the webcam you already have.
2. Leave **Output** on `UDP over network`, host `127.0.0.1`, port `4242`.
3. Press **Start**.

### Phone App Setup

This mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone app works here if it sends that protocol itself, or ships a PC-side companion that does. Check your app against that first.

For an app that does send it, what decides the wiring is how much filtering the app does before the packet leaves the phone:

- **Sending direct.** Point the app at this PC's LAN address (`ipconfig` finds it) on UDP port `4242`. I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket; it filters on-device, so it can send direct. Any other app that filters as much works the same way.
- **Through OpenTrack.** A raw or lightly filtered feed will jitter if you send it direct, because this mod's smoothing is sized to take the edge off a clean signal rather than to rescue a noisy one. Send from the phone into OpenTrack on a spare port (`5252`, say, opened in your firewall), set OpenTrack's output to `127.0.0.1:4242`, and its filters and curves clean the feed up on the way through. Use this route too if you want OpenTrack's curve mapping.

The test is quick: try direct, hold your head still, and if the view drifts or shakes, route it through OpenTrack.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a tracker running on this same PC that sends to your LAN address instead of `127.0.0.1`, because the mod classifies the transport, not the machine. Send to `127.0.0.1` if you want `LocalSmoothing`.

## Controls

Every action has two bindings that do the same thing, so use whichever your keyboard has: the nav cluster if you have one, the chord if you are on a tenkeyless or laptop board.

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Toggle tracking | `End` | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up` | `Ctrl+Shift+G` |
| Toggle yaw mode | `Page Down` | `Ctrl+Shift+H` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches head yaw between turning about world up and turning about the view axis.

### Aiming down sights

Head tracking stays on while you aim. The weapon stays where your mouse or
controller points it, so with your head turned it sits off to one side with its
sights still lined up, and your rounds land where those sights point. Head
movement is scaled to the zoom, so a scope does not magnify it. Leaning eases
out while the sights are up, because it would move your eye off them.

## Windowed play

If you play windowed, the mod centres the game window on the work area of the
monitor it opened on, once, after the game has finished placing it itself, and
only when it is not centred already. A fullscreen or borderless window fills the
screen and is left where it is, as is one you have already centred. The mod
never moves the window again after that one move.

## Configuration

The mod writes `HeadTracking.ini` next to `WolfNewOrder_x64.exe` on first
launch. Edit it in any text editor and restart the game. Deleting it restores
every default the next time the game starts.

```ini
[Network]
; The port your tracker sends OpenTrack UDP packets to.
UdpPort=4242

[General]
EnableOnStartup=1
; 1 = head yaw turns about world up, so a glance left stays level while you
; are looking up or down a stairwell. 0 = yaw turns about the view axis.
WorldSpaceYaw=1

[Hotkeys]
; Windows virtual key codes, in hex. Each action has a nav-cluster key and a
; Ctrl+Shift+<key> chord, and both fire it - remap either or both.
; Common codes: End 0x23, Delete 0x2E, PgUp 0x21, PgDn 0x22,
; F1-F12 0x70-0x7B, A-Z 0x41-0x5A, numpad 0-9 0x60-0x69.
ToggleKey=0x23
CycleModeKey=0x21
YawModeKey=0x22
ChordToggleKey=0x59
ChordCycleModeKey=0x47
ChordYawModeKey=0x48

[Rotation]
; Smoothing covers rotation and position alike, and the value used is picked
; per connection from where the tracker sends from. 0.0 none .. 1.0 heavy.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
Enabled=1
; How far the view may lean from where the game put it, in metres.
; 0 to 0.50 on each axis.
LimitX=0.30
LimitY=0.20
LimitZ=0.40
LimitZBack=0.10
```

Everything here is read at launch, and the mod never writes to this file after
creating it.

The tracker owns the shape of the pose, so set sensitivity, deadzone and axis
inversion in OpenTrack or your phone app once and every game behaves the same
way.

Field of view is a game setting: `OPTIONS` > `VIDEO` > `FOV`, a slider under
`BRIGHTNESS` that ships at 80. The mod reads the field of view the game is
rendering the frame with, every frame, and never writes it, so that slider is
the only thing setting your FOV and head tracking follows whatever you put it
at.

## Troubleshooting

The mod writes `HeadTracking.log` next to `WolfNewOrder_x64.exe`, starting a fresh file each launch and keeping the previous one as `HeadTracking.prev.log`. It records the game build it matched, whether the hooks installed, and whether the UDP port was bound or is still being retried. Read it first.

**Mod not loading (no log file at all)**

- Confirm `dinput8.dll` sits next to `WolfNewOrder_x64.exe`.
- Confirm you have the **x64** Ultimate ASI Loader. The x86 build silently fails to load into a 64-bit process.
- Confirm `WolfensteinTheNewOrderHeadTracking.asi` is in the same folder, spelled exactly as shipped.

**No tracking response**

- Check your tracker is running, with output set to UDP `127.0.0.1:4242`, or to this PC's LAN address on port `4242` from a phone.
- Check Windows Firewall is not blocking UDP port `4242`.
- If port 4242 could not be bound at launch, the log records it along with the reason Windows gave - `error 10048` is the port already being in use, which usually means another game or a second copy of OpenTrack has it. Close that app and keep playing: the mod retries every 500ms in the background, so tracking comes up within about half a second of the port freeing, with no restart.
- Check the log for a build mismatch. On a game build it does not recognise, the mod installs no hooks and lets the game run vanilla.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` if the tracker is a phone or another device on the network.
- Route a lightly filtered phone feed through OpenTrack instead of sending it direct, so OpenTrack's filters can clean it up.
- Improve your lighting for webcam tracking. A dim or backlit face is the usual cause.

**Wrong rotation axis (the view turns the wrong way)**

- Fix it in the tracker, not in the game. This mod has no axis-inversion setting on purpose: OpenTrack's Mapping tab inverts an axis for every game at once, so one change there covers everything. If you send from a phone app instead, look for the equivalent in its own settings.
- Check you mapped the axes you meant, and that a leftover profile is not driving yaw from head roll.
- If an axis is still wrong with a correctly configured tracker, that is a bug in the mod. Please report it.

**The weapon is off to one side when I aim down sights**

- Your head is turned: the weapon stays on your aim and you are looking past it. Turn back to it, or move your aim to where you are looking.

**Yaw feels wrong when looking up or down at extreme angles**

- Press `Page Down` / `Ctrl+Shift+H` to switch yaw mode. The default turns head yaw about world up, so a glance stays level however far up or down you are already looking; the other position turns it about the view axis, which leans the horizon as you glance.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The mod loader (Ultimate ASI Loader) is only removed if the installer put it there, since another mod may be relying on it. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires [pixi](https://pixi.sh), Visual Studio with the C++ workload, and CMake. No game install is needed: the build reads repo files only.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/wolfenstein-the-new-order-headtracking
cd wolfenstein-the-new-order-headtracking
pixi run test
pixi run package
```

`pixi run package` writes the installer ZIP to `release/`. `pixi run install` builds and deploys straight into a detected game install for the dev loop.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

Third-party components redistributed in the release ZIPs are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). This mod contains no game code, no extracted assets and no game data files, and it requires a legitimately purchased copy of the game.

## Credits

- MachineGames and Bethesda Softworks, developer and publisher of Wolfenstein: The New Order, which is copyright ZeniMax Media Inc. The Wolfenstein name is a trademark of id Software LLC
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, the ASI loader (MIT)
- [OpenTrack](https://github.com/opentrack/opentrack), the head tracking protocol and software (ISC)
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core), the shared tracking pipeline (MIT)

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by MachineGames, Bethesda Softworks, ZeniMax Media or id Software. Use at your own risk.
