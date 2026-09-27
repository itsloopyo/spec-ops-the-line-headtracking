# Spec Ops: The Line Head Tracking

![Spec Ops: The Line running with this mod](https://raw.githubusercontent.com/itsloopyo/spec-ops-the-line-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Spec Ops: The Line that moves the camera with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the rendered view; aim stays on your mouse or controller
- **6DOF tracking** - yaw, pitch and roll plus positional lean and peek
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Spec Ops: The Line on [Steam](https://store.steampowered.com/app/50300/) (App ID 50300).
- A tracking source that speaks the OpenTrack UDP protocol. [OpenTrack](https://github.com/opentrack/opentrack) is free and takes input from webcams, phones, and VR headsets.
- Windows 10 or 11. The game is 32-bit, so the mod ships as a 32-bit `.asi`.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Spec Ops: The Line**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the [Releases page](https://github.com/itsloopyo/spec-ops-the-line-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. In OpenTrack, set the output to UDP and send to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, pass the install folder as the first argument:

```powershell
install.cmd "D:\Games\SpecOps_TheLine"
```

### Manual Installation

The installer places two files in the game's `Binaries/Win32/` folder. To do it by hand:

1. Extract the Ultimate ASI Loader (`dinput8.dll`) from `vendor/ultimate-asi-loader/` into `Binaries/Win32/`. If a `dinput8.dll` from another mod is already there, leave it in place; the loader only needs to exist once.
2. Copy `SpecOpsTheLineHeadTracking.asi` into the same folder.

The full path is usually:

```
<SteamLibrary>/steamapps/common/SpecOps_TheLine/Binaries/Win32/
```

The Nexus release ZIP contains only `SpecOpsTheLineHeadTracking.asi`, for users who already run an ASI loader.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord          |
|---------------------|-------------|----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H` |

Centring is done in your tracker app: Center in opentrack, CENTER in Headcam, or the equivalent in whatever you run.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode: full tracking, then rotation only, then position only, then back to full.

`Page Down` / `Ctrl+Shift+H` switches head yaw between horizon-locked and camera-local. Horizon-locked is the default and keeps "up" where it is however the mouse is pitched.

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` as soon as you change them, and come back at the next start. `End` / `Ctrl+Shift+Y` changes the current session only: whether tracking is on at startup is `EnableOnStartup`.

Each action's keys are a list in the `[Hotkeys]` section of `CameraUnlock.ini`, the chord included, so any of them can be rebound or removed.

## Configuration

Apart from creating `CameraUnlock.ini` at startup when there is none, the mod writes to it only when a hotkey changes the tracking mode or the yaw mode. It never writes `SpecOpsTheLineHeadTracking.ini`, and it creates `Defaults.ini` only when there is none and never changes it. Edit `CameraUnlock.ini` with the game closed.

<!-- cameraunlock:config -->
The mod reads its settings from `Binaries\Win32\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `DataFreshnessMs=500`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `CollisionEnabled=true`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Spec Ops: The Line head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default
; Milliseconds a tracker packet stays current. Once the tracker has sent nothing
; for this long, the mod stops following it until data arrives again.
DataFreshnessMs=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default
; true: leaning stops at walls instead of moving the view through them.
CollisionEnabled=default
; How far, in centimetres, the view is held off a wall when you lean into it.
CollisionMargin=10.0

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[FieldOfView]
; Multiplies the field of view the game renders with: 0.5 to 2.0. 1.0 leaves it alone;
; the game draws 72 degrees horizontally from the hip, so 1.25 gives 90. Only the
; picture widens: the game keeps its own value for everything it decides with, so
; shots land exactly where they did. Aiming down sights is scaled by the same factor,
; so the sights keep their relative zoom.
FovScale=1.0
```
<!-- /cameraunlock:config -->

`FovScale` under `[FieldOfView]` widens the picture. Spec Ops has no field of view setting of its own; it renders 72 degrees horizontally from the hip and 50 down the sights, so `FovScale=1.25` gives 90 and 62.5. Aiming down sights is multiplied by the same factor, which keeps the sights' relative zoom, and shots land exactly where they did.

`CollisionEnabled` stops a lean at the level's geometry. The game places and collides its own camera before the head pose is added, so nothing in the engine knows the view has leaned: without this, leaning towards cover walks the camera into it and you see the level from inside the wall. The mod casts the lean as a ray from the camera the game chose - the same trace the game resolves its crosshair with - and shortens the lean to what it reaches, keeping its direction. `CollisionMargin` is how far short of the surface the view stops, in centimetres. The crosshair follows the shortened lean, so it still marks the point the shot lands on. The clamp needs the game to have run its own crosshair trace once, which happens in the first frames of gameplay; before that a lean is unbounded.

**Smoothing is chosen per connection, and only loopback counts as local.** A tracker sending to `127.0.0.1` gets `LocalSmoothing`; anything else, including a tracker running on this very PC that sends to the machine's own LAN address, is classified remote and gets `RemoteSmoothing`.

The game constrains its picture to 16:9 whatever the window is, so at any other resolution it draws into a letterboxed band with black bars. The crosshair is placed inside that band rather than the window, which matters as soon as you move off centre.

The game's crosshair always follows the aim point, and there is no setting for sensitivity, inversion or deadzone: set those in your tracker.

## Troubleshooting

**Mod not loading**
- Confirm `dinput8.dll` and `SpecOpsTheLineHeadTracking.asi` are both in `Binaries/Win32/`.
- Launch through Steam, not by running `SpecOpsTheLine.exe` directly.
- Look for `SpecOpsTheLineHeadTracking.log` in `Binaries/Win32/`. If it is missing, the loader did not pick up the `.asi`. The previous session is kept as `SpecOpsTheLineHeadTracking.prev.log`.

**No tracking response**
- Make sure OpenTrack is running and Started, with output set to UDP on `127.0.0.1:4242`.
- Check that port `4242` is not blocked by your firewall.
- Open `SpecOpsTheLineHeadTracking.log` and read the heartbeat line about whether OpenTrack data is being received.

**Another game was still open on port `4242`**
- Only one process at a time can listen on the tracker port, so if you start Spec Ops while another head tracking mod is still running, this mod cannot bind and the log says `Failed to bind UDP port 4242 ... retrying every 500ms until it is free`.
- Do not restart Spec Ops. Close the other game and leave this one running: the mod retries the bind twice a second for as long as it is loaded, and picks the port up on its own. The log line `Bound UDP port 4242 after Ns of waiting - tracking is live` is the confirmation, and tracking resumes from there with no further action.
- While it waits it says so every 30 seconds, as `Still waiting for UDP port 4242`.

**Jittery or unstable tracking**
- Raise `LocalSmoothing` or `RemoteSmoothing` toward `1.0` in `CameraUnlock.ini`. Which one applies is decided by the packet's source address, not by which machine the tracker runs on: only `127.0.0.1` counts as local, so a tracker on this PC that sends to your LAN address gets `RemoteSmoothing`.
- Add a small deadzone in your tracker (OpenTrack's Filter tab, or the phone app's own setting) to ignore tiny head movements.
- For wireless or phone trackers, increase smoothing in the tracker app as well.

**Wrong rotation axis**
- Invert that axis in your tracker. OpenTrack has a per-axis Invert checkbox on its Mapping tab, and phone apps generally have the equivalent. The mod applies the pose it is sent at 1:1 and has no inversion of its own, so one tracker profile behaves the same way in every game.

**The crosshair disappears when I turn my head a long way**

- That is deliberate. The crosshair marks the point your shot lands on, and once your head turns far enough that point is no longer on screen. Drawing the crosshair anyway would put it somewhere the shot does not go, so it is hidden until the aim point comes back on screen. With a 72 degree field of view it starts happening around 40 degrees of head yaw.

**Yaw feels wrong when looking up or down at extreme angles**
- Toggle between world-locked and camera-local yaw with `Page Down` or `Ctrl+Shift+H`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's `.asi` and its two log files, and leaves your INI in place. The Ultimate ASI Loader (`dinput8.dll`) is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires Visual Studio 2022 or newer with the C++ workload, and CMake. The build does not pin a Visual Studio version - CMake uses whichever one is installed.

```bash
git clone --recurse-submodules https://github.com/itsloopyo/spec-ops-the-line-headtracking
cd spec-ops-the-line-headtracking
pixi run build-release
```

Output lands at `bin/Release/SpecOpsTheLineHeadTracking.asi`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- Spec Ops: The Line developed by Yager Development, published by 2K Games (Take-Two Interactive).
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG (MIT).
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause).
- [OpenTrack](https://github.com/opentrack/opentrack) (ISC).
- Built on the shared [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) framework.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Yager Development or 2K Games. Use at your own risk.
