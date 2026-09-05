# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Added camera collision for positional tracking, `[Collision] Enabled` (default on) and
  `[Collision] Padding` (default 10 world units). The head pose moves the render eye after
  the game has already placed and collided its own camera, so a lean towards cover used to
  walk the view straight into the wall. The lean is now cast as a ray from the camera the
  game chose, using the game's own crosshair trace parameters, and shortened to what it
  reaches - it keeps its direction and loses only its length. The reticle is projected with
  the shortened lean, so the crosshair still marks the point the shot lands on.
- Added a field of view setting, `[FieldOfView] Scale`. Spec Ops ships no way to change its
  field of view; it renders 72 degrees horizontally from the hip and 50 down the sights.
  The setting multiplies that, so 1.25 gives 90 and 62.5 and the sights keep their
  relative zoom. It reaches the projection the frame is drawn with and nothing else: the
  accessor the scene-view builder reads has two other callers, both game logic, and those
  keep the game's own angle, so nothing about where shots go changes. Measured in game at
  1.5: the rendered field of view read back off the pixels as 109 degrees against the 108
  asked for, and the crosshair stayed on the aim point.
- Added `[General] ShowAimMarker` (default off), which draws the mod's own marker at the
  point the shot lands, over the game's frame, for diagnosis.

### Changed
- Stripped the third-party DLLs Ultimate ASI Loader carries as resources out of the
  vendored copy. The upstream 32-bit build embeds `binkw32.dll` (RAD Game Tools' Bink
  and Smacker 1.994i, proprietary middleware licensed per title), `wndmode.dll` (DirectX
  Windower Embedded, (C) 2008 VEG and (C) 2004 menopem, no licence) and `vorbisfile.dll`
  (Xiph.Org, BSD-3-Clause) so that a user who renames the loader over one of those
  libraries still gets the original exports. The installer ZIP ships that binary, so it
  was redistributing all three. `scripts/strip-loader-payload.ps1` now zeroes them,
  `pixi run update-deps` runs it on every refresh, and `pixi run package` refuses to
  build a ZIP from a loader that still has them. Only the `.rsrc` section changes: the
  loader's code, imports, relocations and appended PDB are byte-identical to upstream,
  and nothing in this mod could reach the stripped resources anyway.
- Changed smoothing to two `[Smoothing]` keys: `LocalSmoothing` (default `0.0`, tracker
  running on this PC) and `RemoteSmoothing` (default `0.15`, tracker on a remote network
  device). The value is picked per connection from the packet source address and covers
  both rotation and position.
- Dropped the mod's own centre, so the tracker pose is applied as absolute. Every tracker
  app centres itself, so a mod-side centre sat in series with the tracker's and the two
  drifted apart. Centre in your tracker app instead. The recentre hotkey and the
  `[Hotkeys] Recenter` / `ChordRecenter` keys went with it.
- Logged a successful UDP bind with the port. Only the failure was logged, so a reader had
  to infer the healthy case from an absent warning.
- Stopped the heartbeat repeating the camera-detour call count every 5 seconds (720
  identical lines an hour, ~44 KB, burying the startup chain). It is now edge-triggered
  like the OpenTrack line: one line when the detour starts or stops firing.
- Cut the per-call viewpoint proof-of-life telemetry down to a single line. It is now one
  `First tracked frame` line naming the pose that reached the render view, written once,
  in place of a sample stream that ran for the whole session.
- Kept one previous log generation. The log already started fresh on every launch; the
  session before a crash is now kept as `SpecOpsTheLineHeadTracking.prev.log`.
- Documented in the README that starting Spec Ops while another head tracking mod still
  holds port 4242 is recoverable. The mod already retries the bind twice a second for as
  long as it is loaded and takes the port over on its own once the other game exits, but
  nothing said so, so the obvious response was to quit and relaunch.

### Fixed
- A tracker that goes quiet now holds its last pose instead of snapping the view back to
  the game's camera. Past `DataFreshnessMs` the mod injected nothing at all, so a face
  the webcam lost for half a second, or two dropped datagrams over WiFi, threw the view
  across the screen and back - and because the smoothing only runs while samples arrive,
  nothing blended it. The README already documented the hold.
- `[Position] LimitY` now bounds only upward travel, and a new `LimitYDown` bounds
  downward travel. The downward clamp was never configured at all and sat on the core
  library's own 0.20 whatever the INI said, so raising `LimitY` to 0.45 gave 0.45 up and
  0.20 down with nothing to explain the asymmetry.
- The reticle no longer writes an unbounded position into the game's HUD. The guard that
  hides the crosshair when the aim swings off the frame compared a raw vector component
  against a threshold that only reads as an angle for a unit direction; once a trace
  distance was folded in, that component was the hit distance in centimetres, so the
  guard admitted aim points a hundredth of a degree off the view plane and the crosshair
  was moved millions of pixels. The guard is now taken on the direction cosine, and a
  frame whose aim point cannot be placed draws no crosshair rather than a misplaced one.
- A tracker pose that is not a finite number is dropped at the engine boundary and named
  once in the log, instead of being written into the camera. It reached the rotator and
  the camera's world position, where a NaN renders a black frame, and in the camera-local
  yaw mode it also reached `lround`, which is undefined for one.
- Head pitch stacked on an already-steep game camera is stopped one unit short of
  vertical instead of passing it and inverting the world.
- A positional lean is applied in a genuinely horizon-locked basis. The forward axis was
  the camera's pitched forward, which is not perpendicular to world up, so with the
  camera pitched 40 degrees down a 0.40m forward lean also dropped the eye 0.26m - travel
  the vertical limits could not see, because they are applied before this basis.
- Resuming after a tracking gap blends instead of snapping. The frame clock was not
  ticked while the pose was held, so the first fresh frame arrived with the whole gap as
  its delta - clamped to 0.25s, at which the smoothing factor is 0.99998, i.e. a jump.
- Cycling the tracking mode while the tracker is quiet now takes effect immediately. The
  held pose carried its own rotation and position flags and never passed through the
  mode, so switching to rotation-only to get rid of a stale lean did nothing until
  packets resumed.
- The aim trace no longer hands the engine a pointer to a freed actor. The captured trace
  parameters name the player's own pawn, and they are now dropped the moment the player
  controller stops possessing one, so a death, a level load or a chapter change cannot
  leave a stale actor to be traced against.
- Only the outermost crosshair draw applies the offset, and the restore is scope-bound.
  A missed restore compounded rather than costing one frame: the saved value is read back
  out of the field each time, so a field left offset was offset again next frame, and
  nothing else in the HUD rewrites it.
- The aim marker is drawn against an explicit viewport. Pre-transformed geometry is
  clipped against whatever viewport the last pass left bound, so a leftover from a
  downsampled post-process step could clip the marker away while the log still reported
  it drawn.
- `DLL_PROCESS_DETACH` no longer joins threads or unpatches the game when the process is
  already exiting. Both of those suspend or wait on threads Windows has already killed,
  which can hang the game on exit with nothing to attribute it to.
- A hotkey thread that cannot be created is reported and leaves the mod dormant, instead
  of terminating the game. The poller throws rather than failing silently, and the
  exception was escaping a thread entry point.
- The mod refuses to start rather than reading an INI that is not yours. When its own
  module directory could not be resolved it fell back to the bare filename, which
  `GetPrivateProfileString` resolves against the Windows directory - so every setting
  silently read as its default. The log path is now resolved as UTF-16 throughout, so a
  game installed under a non-ASCII path gets a log file.
- The mod loads on a game installed under a path the system codepage cannot spell. It
  resolves its own directory as UTF-16, and where the INI has to be handed to core as a
  narrow string it uses the directory's 8.3 alias, which is the same path spelled in
  ASCII. Only a volume with 8.3 aliases turned off has no such spelling, and that is
  refused rather than guessed at. It never falls back to a bare filename, which
  `GetPrivateProfileString` resolves against the Windows directory - so a failure to
  resolve the path now stops the mod instead of silently reading someone else's file.
- `uninstall.cmd` removes `SpecOpsTheLineHeadTracking.log` and `.prev.log`, which it
  previously left behind despite the changelog saying otherwise. Your INI is left in
  place, because the launcher may run uninstall as one half of an update.
- `install.cmd` and `uninstall.cmd` are CRLF again. `install.cmd` had been saved with
  Unix line endings, which makes a batch file fail silently on Windows.
- A failed INI write says so, naming the directory and the Windows error, rather than
  surfacing one step later as "Failed to open INI" - which reads as a corrupt file rather
  than a folder the game cannot write to.
- Fixed the crosshair drifting vertically at any resolution that is not 16:9. The game
  constrains its scene view to a fixed aspect ratio and letterboxes the result inside the
  window - measured at 1280x800, the world is drawn into a 1280x720 band 40 pixels down -
  so the projection's coordinates span that band, and the game's own crosshair rests at
  the middle of it rather than of the window. Both the crosshair reposition and the mod's
  marker were scaling by the window instead, which is invisible at screen centre and
  grows with the offset. They now follow the band the frame is actually drawn into, and
  the vertical field of view comes from the ratio the projection was built with rather
  than the window's.
- Suppressed head tracking outside gameplay. The game builds its scene view at the full
  frame rate in the main menu and behind the pause screen alike, so the view used to keep
  swinging around while you were reading a menu. Tracking is now suppressed unless the
  player controller possesses a pawn (which the front end never does) and the world is
  not paused, which also covers loading and level transitions, where the world chain is
  empty. The mod's own aim marker and the crosshair reposition follow the same gate.
- Decoupled look and aim. The head pose is added to the viewpoint only when the caller is
  the scene-view builder, identified by the address it returns to; the other twenty
  callers - aim, traces, interaction - keep the clean viewpoint, so the game shoots where
  the mouse points while the view follows your head.
- Moved the game's own crosshair onto the point the weapon is aimed at, instead of leaving
  it at screen centre where it no longer marks the shot. Every crosshair the game selects
  goes through one function on its way to being drawn, and its screen position is offset
  across that call and restored immediately, so nothing else on the HUD moves. The offset
  projects the clean aim direction resolved basis-to-basis in the head-tracked view, using
  the field of view that frame was projected with, so it cannot drift out of agreement
  with the camera on a combined pose. The crosshair marks the POINT the shot lands on, not
  the direction it leaves along, so a positional lean no longer drags it off the target.
  Leaning moves the rendered eye away from the eye the shot comes from, and a
  direction-based crosshair slides off what it is marking by roughly the lean divided by
  the distance - worse the closer the target. The distance comes from the game's own
  crosshair ray, cast with the game's source actor, trace flags and range on the frame
  that consumes it, so the crosshair stops on the same surfaces the bullet does. No
  smoothing and no fixed convergence range: when the aim crosses an edge the impact point
  genuinely jumps and the crosshair jumps with it. A ray that hits nothing is a target at
  infinity, and the aim direction is projected instead.

### Removed
- Removed the pose-shaping settings: `[Sensitivity] Yaw/Pitch/Roll`,
  `[Sensitivity] InvertYaw/InvertPitch/InvertRoll`, `[Smoothing] DeadzoneDeg`,
  `[Position] SensitivityX/Y/Z` and `[Position] InvertX/Y/Z`. The mod now uses the
  tracker's pose at 1:1. Sensitivity, deadzones and axis inversion belong in OpenTrack or
  the phone app, where one profile then behaves the same way in every game rather than
  having to be re-tuned per title. An existing INI that still sets any of them loads
  normally and gets one log line naming the change. Protocol-to-engine sign conversion is
  unaffected - that is a fact about UE3 rather than a preference, and it stays in the
  camera hook.
- Removed `[Smoothing] Smoothing` and `[Position] Smoothing`.
- Removed the hidden 0.15 baseline smoothing floor, so a local tracker now gets
  zero-latency, unsmoothed tracking by default.

## [0.0.0] - 2026-06-03

### Added
- Initial scaffold. C++/ASI head-tracking project skeleton for Spec Ops: The Line (Unreal Engine 3, Win32), installed via an Ultimate ASI Loader dinput8.dll proxy. OpenTrack UDP receiver, hotkey poller, INI config, and PE-fingerprint logging.
- Camera hook: MinHook detour on APlayerController::GetPlayerViewPoint (RVA 0x6B83F0) injecting head rotation (UE3 int32 FRotator) and a 6DOF position offset (applied in the camera's clean-orientation basis) into the per-frame render viewpoint. Append-only build-profile registry keyed on PE fingerprint (steam-win32-20120716); the mod stays dormant on any unrecognised build.
- 6DOF positional tracking with per-axis limits and a configurable world-unit scale (cm per metre). Page Up / Ctrl+Shift+G cycles the tracking mode.
- Controls: End/PageUp/PageDown plus Ctrl+Shift+Y/G/H chord alternatives.
- Verified in-game: loader engages, build profile matches by fingerprint, hook installs, OpenTrack data flows, no crash.
