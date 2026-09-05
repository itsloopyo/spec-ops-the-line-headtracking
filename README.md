# Spec Ops: The Line Head Tracking

![Spec Ops: The Line running with this mod](https://raw.githubusercontent.com/itsloopyo/spec-ops-the-line-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Spec Ops: The Line that moves the camera with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the rendered view; aim stays on your mouse or controller
- **Parallax-correct crosshair** - the game's own crosshair is moved onto the point the shot lands on, not just its direction
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- Spec Ops: The Line on [Steam](https://store.steampowered.com/app/50300/) (App ID 50300).
- A tracking source that speaks the OpenTrack UDP protocol. [OpenTrack](https://github.com/opentrack/opentrack) is free and takes input from webcams, phones, and VR headsets.
- Windows 10 or 11. The game is 32-bit, so the mod ships as a 32-bit `.asi`.

## Installation

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

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

The config file is generated on first run next to `dinput8.dll` at `Binaries/Win32/SpecOpsTheLineHeadTracking.ini`. Defaults follow CameraUnlock standards.

Put each comment on its own line, above the key. A `true`/`false` or text setting compares the whole text after `=`, so a trailing `; note` makes the comparison fail and the setting silently keeps its default. Numeric settings read the number off the front of the text, which is why some lines below still carry one.

```ini
[General]
; Start with tracking active
EnableOnStartup=true
Port=4242                  ; UDP port OpenTrack sends to
DataFreshnessMs=500        ; Hold the last pose if no packet arrives within this window (ms)
; Yaw mode: true = horizon-locked yaw (default), false = camera-local
WorldSpaceYaw=true
; Draw the mod's own marker at the aim point as well. The game's own
; crosshair is already moved there, so this is a diagnostic.
ShowAimMarker=false

[FieldOfView]
; Multiplies the field of view the game renders with. The game draws 72 degrees
; horizontally from the hip, so 1.25 gives 90. Range 0.5-2.0.
Scale=1.0

[Smoothing]
; 0.0 = responsive, 1.0 = heavy. Covers rotation and position. The value is
; picked per connection from the packet source address.
LocalSmoothing=0.0         ; Tracker running on this PC (loopback)
RemoteSmoothing=0.15       ; Phone or other device on the network

[Position]
; 6DOF positional tracking: leaning moves the camera as well as turning it.
; Turn the whole section off here.
Enabled=true
LimitX=0.30                ; Max lateral lean in metres of head movement
LimitY=0.20                ; Max upward move in metres
LimitYDown=0.20            ; Max downward move in metres (separate budget: ducking has less room than stretching up)
LimitZ=0.40                ; Max forward lean in metres
LimitZBack=0.10            ; Max backward lean in metres (kept short so the camera does not clip through the player)
PositionScale=100.0        ; World units (cm) per metre of head translation - the main tuning knob. Higher = more camera movement for the same lean

[Collision]
; Stop a lean at the level's geometry instead of pushing the view through a wall.
Enabled=true
; How far short of a surface the leaned view stops, in world units (cm). 0-100.
Padding=10.0

[Hotkeys]
; Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode).
Toggle=0x23
CycleMode=0x21
YawMode=0x22
; Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode).
ChordToggle=true
ChordCycleMode=true
ChordYawMode=true
```

`WorldSpaceYaw=true` (default) keeps yaw rotating around the world up-axis, so "up" stays gravity-aligned even when you look up or down. Set it to `false` for camera-local yaw, which follows the camera's current up-axis. Toggle it at runtime with `Page Down` or `Ctrl+Shift+H` without restarting.

`FieldOfView.Scale` widens the picture. Spec Ops has no field of view setting of its own; it renders 72 degrees horizontally from the hip and 50 down the sights, so `Scale=1.25` gives 90 and 62.5. Aiming down sights is multiplied by the same factor, which keeps the sights' relative zoom, and shots land exactly where they did.

`Collision.Enabled` stops a lean at the level's geometry. The game places and collides its own camera before the head pose is added, so nothing in the engine knows the view has leaned: without this, leaning towards cover walks the camera into it and you see the level from inside the wall. The mod casts the lean as a ray from the camera the game chose - the same trace the game resolves its crosshair with - and shortens the lean to what it reaches, keeping its direction. `Padding` is how far short of the surface the view stops. The crosshair follows the shortened lean, so it still marks the point the shot lands on. The clamp needs the game to have run its own crosshair trace once, which happens in the first frames of gameplay; before that a lean is unbounded.

The game constrains its picture to 16:9 whatever the window is, so at any other resolution it draws into a letterboxed band with black bars. The crosshair is placed inside that band rather than the window, which matters as soon as you move off centre.

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
- Raise `LocalSmoothing` or `RemoteSmoothing` toward `1.0` in the INI. Which one applies is decided by the packet's source address, not by which machine the tracker runs on: only `127.0.0.1` counts as local, so a tracker on this PC that sends to your LAN address gets `RemoteSmoothing`.
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
