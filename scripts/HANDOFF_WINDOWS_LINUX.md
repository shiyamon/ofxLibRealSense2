# Hand-off: bringing ofxLibRealSense2 up on Windows and Linux

Notes for whoever (person or agent) builds and tests this addon on Windows and Linux. macOS is done
and tested; this file says what exists, what still has to happen per platform, and how to check it.

## Where things stand

- **librealsense v2.59.1** (`c84fe6438`), built from source with `scripts/build_librealsense_*`.
  Use the same version on the other platforms unless there's a reason to move, and note it.
- **macOS is finished and hardware-tested**: M3 Ultra, macOS 27, Xcode 27, openFrameworks git
  master, RealSense D415 (firmware updated to the current release during testing). All five
  examples build from projectGenerator projects with no edits and stream. Static universal
  library (arm64 + x86_64) in `libs/librealsense2/lib/osx`.
- **Windows and Linux have not been built yet.** `build_librealsense_windows.ps1` and
  `build_librealsense_linux.sh` match the macOS options but have never run: expect small fixes.
  There are no Windows / Linux libraries in the repo (`lib/vs`, `lib/linux64` don't exist).
- The addon code (`src/`) is platform-neutral C++17 and only uses librealsense's public API; it
  has only been compiled with Apple Clang so far. MSVC and GCC may find things Clang didn't.

## Rules

- **Don't touch the macOS side:** `libs/librealsense2/lib/osx`, `build_librealsense_macos.sh`, the
  `osx:` section of `addon_config.mk`. The headers in `libs/librealsense2/include` are shared:
  every script rewrites them from the same librealsense checkout, so use the same version.
- **Keep the CMake options identical across platforms** (table in `scripts/README.md`). If one has
  to change on one platform, write down why there.
- Keep the API the same as ofxKinectV2's where it overlaps (see the main README); the examples are
  shared with it apart from the class name.
- Prefer fixing build problems in the scripts over patching librealsense.
- Generated projects, `bin/` (except `bin/data`), `obj/`, `dll/` are git-ignored: commit sources,
  scripts, `addons.make`, `bin/data` and the libraries for your platform.

## How the addon works (what to look at when something's off)

- `ofxLibRealSense2` is an `ofThread` per camera. The capture thread waits on an `rs2::pipeline`,
  runs the filters, converts to `ofPixels`, computes the depth -> colour map, builds the point
  cloud and hands everything to `update()` under the lock (double-buffered, as ofxKinectV2).
- `src/ofxLibRealSense2Processing.h` holds the multi-threaded spatial filter and depth -> colour
  align. Both are transcriptions of librealsense's own code and were checked against it on live
  frames: the spatial filter is bit-identical; align differs on ~0.03% of pixels (depth edges)
  from librealsense's NEON version on ARM. On x86 librealsense uses an SSE align: worth comparing
  again. The check on macOS was a small program running `rs2::spatial_filter` / `rs2::align` and
  the addon's versions on the same live frames and counting differing pixels.
- One shared `rs2::context` (`getContext()`); disconnection comes from its devices-changed
  callback, which must only call `was_removed()` - listing new devices inside it makes
  librealsense open them and race a camera being opened (that broke opens on macOS).
- Camera options are applied from the parameters at open (parameters are the source of truth):
  a camera can be left with the laser off by another app.

## Windows

### What you need

- Windows 10 / 11 x64.
- **Visual Studio 2022 or 2026** with the "Desktop development with C++" workload. It brings the
  MSVC compiler, the Windows SDK, CMake and Ninja; the script uses VS's own CMake and Ninja.
  Use the same Visual Studio (platform toolset) as openFrameworks is built with: a mismatch fails
  at link time with `C1900: Il mismatch between 'P1' version ... and 'P2' version ...` (whole
  program optimisation). Generate with the `vs2026` template, or set Project > Properties >
  General > Platform Toolset to match `openframeworksLib`. (Found with ofxKinectV2.)
- Git, and PowerShell (5.1 or 7). If scripts are blocked:
  `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.
- openFrameworks (tested with git master on macOS; ofxKinectV2 is tested on Windows with it).
- Not needed: the RealSense SDK installer, CUDA, Zadig / driver changes, admin rights for the camera.

### Build librealsense

```powershell
git clone https://github.com/IntelRealSense/librealsense C:\dev\librealsense
cd C:\dev\librealsense; git checkout v2.59.1
cd C:\path\to\openFrameworks\addons\ofxLibRealSense2\scripts
.\build_librealsense_windows.ps1 -Source C:\dev\librealsense
```

What the script does: enters the VS developer shell (x64), configures with Ninja (single config,
Release) and the options in `scripts/README.md`, builds only the `realsense2` target, then copies

```
libs/librealsense2/include/librealsense2/      (rewritten from the checkout - same as macOS)
libs/librealsense2/lib/vs/x64/realsense2.lib   import library
libs/librealsense2/lib/vs/x64/realsense2.dll   static C runtime: nothing else to ship
```

The configure step downloads librealsense's own dependencies (nlohmann json, etc.), so it needs
network access the first time. One Release DLL serves Debug and Release apps (C API underneath).

### Build the examples

1. projectGenerator: import `example-streams` (addons `ofxGui`, `ofxLibRealSense2`), template
   `vs` / `vs2026` to match openFrameworks, Update.
2. Open the `.sln`, build **Release x64**, then **Debug x64**.
3. Check `realsense2.dll` ended up next to the exe in `bin/` (see below), plug in the camera
   (USB 3 port and cable), run. The status line should show the camera, `USB 3.x`,
   librealsense 2.59.1 and ~30 fps.
4. Repeat for the other four examples, then go through "How to check a platform is done".
5. Update this file, `scripts/README.md` and the main README (the Windows rows), commit
   `libs/librealsense2/lib/vs` and any script / addon fixes.

### Checking the library

- `dumpbin /exports libs\librealsense2\lib\vs\x64\realsense2.dll` should list the C API (`rs2_*`).
- `dumpbin /dependents ...\realsense2.dll`: system DLLs only (no MSVC runtime, no CUDA).
- A camera that's listed but won't open: another app has it (RealSense Viewer, a browser using the
  RGB camera as a webcam, Logitech G HUB...).

Things likely to need attention:

- **The DLL next to the exe.** ofxKinectV2 relies on the projectGenerator copying the DLLs from
  `lib/vs/x64` (via the project's `dll/x64`) next to the exe. Check it happens for
  `realsense2.dll`; if not, add a post-build copy and note it.
- **Frame metadata.** Without it librealsense falls back to host timestamps and some options
  (per-frame exposure etc.) are missing. Intel enables it with a registry script:
  `librealsense\scripts\realsense_metadata_win10.ps1` (run once as administrator with the camera
  plugged in). Check what `getCaptureStats().colorDepthOffsetMs` says with and without it.
- **CUDA** is deliberately off (see `scripts/README.md`): librealsense would only use it for colour
  conversion, and a CUDA build drags in cuBLAS / cuSPARSE DLLs. Only revisit with measurements.
  If you do: `-Cuda` adds sm_52 / sm_61 (librealsense's list skips desktop Pascal, e.g. 1080 Ti).
- **MSVC vs the addon code.** Things Clang accepted that MSVC may not: the `goto`-based state
  machine in `ofxLibRealSense2Processing.h` (a transcription of librealsense's; keep it identical
  rather than restructuring), `thread_local` caches in `ofxLibRealSense2.cpp`, narrowing warnings.
  Fix so Clang and GCC still build.
- On the Windows OF build the first `ofLog` crashed unless `ofLogToConsole()` was called (found
  with ofxKinectV2); all examples call it.
- Several cameras: Windows needs each camera on a USB 3 controller with enough bandwidth, the same
  as elsewhere.

## Linux

### What you need

- Ubuntu 22.04 / 24.04 (x86_64), or aarch64 (Jetson, Raspberry Pi 5) for `linuxaarch64`.
- `sudo apt install build-essential cmake git pkg-config libudev-dev libssl-dev`
- openFrameworks for Linux with its dependencies (`scripts/linux/<distro>/install_dependencies.sh`).

### Build librealsense

```bash
git clone https://github.com/IntelRealSense/librealsense ~/dev/librealsense
git -C ~/dev/librealsense checkout v2.59.1
./scripts/build_librealsense_linux.sh ~/dev/librealsense      # x86_64 -> lib/linux64, aarch64 -> lib/linuxaarch64
```

Then let users open the cameras without root (replug afterwards):

```bash
sudo cp ~/dev/librealsense/config/99-realsense-libusb.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### Build the examples

`cd example-streams && make -j$(nproc) && make RunRelease` (or the projectGenerator for another
IDE). The addon links `-ludev -lpthread` (`addon_config.mk`); the makefiles pick up
`lib/linux64/librealsense2.a` themselves.

Things likely to need attention:

- **The pre-link step** (`ld -r -d --whole-archive ...` then `objcopy --keep-global-symbol='rs2_*'`)
  is the untested part. Check with `nm -g --defined-only librealsense2.a` that only `rs2_*` is
  left, and that an app links (missing symbols: add the library to the `ld -r` list; duplicate
  symbols: as on macOS, rsutils' copy of easylogging++ is dropped - there may be others).
- **Link flags:** `addon_config.mk` adds `-ludev -lpthread` (libusb uses udev). Add anything else the
  linker asks for and record it.
- **Backend:** libusb (RSUSB) by default - no kernel patches. `--v4l2` builds the kernel backend
  Intel's packages use; it needs librealsense's kernel patches for metadata. Try RSUSB first.
- **Several cameras** on Linux with libusb may need a larger USB buffer:
  `echo 1000 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb` (permanent: kernel
  parameter `usbcore.usbfs_memory_mb=1000`).
- Jetson / Raspberry Pi (aarch64): librealsense builds with NEON there. CUDA on Jetson
  (`BUILD_WITH_CUDA_ZEROCOPY`) is a separate project - don't enable it without measuring.

## Findings from macOS (apply everywhere)

- **Apps that grab USB devices block the camera.** Logitech G HUB's `lghub_agent` opens every USB
  device; with it running, opens failed at random with `failed to claim usb interface ... ACCESS`.
  Quit such apps. (Same problem with the Kinect v2.)
- **Update the camera's firmware** (RealSense Viewer or `rs-fw-update`) before chasing problems;
  the test D415 was on 5.12.6.
- librealsense logs `control_transfer returned error ... timed out` warnings on macOS at stream
  start and sometimes while streaming, between 2 and ~100 per 10 s in identical runs, with or
  without the addon and with no frame loss. They're hidden by default now (librealsense logs
  errors only unless `OF_LOG_VERBOSE`). Check whether Windows / Linux show them at all.
- At stream start a few frames are counted as dropped (frame counter gaps): not real loss.
- macOS needs root (libusb vs the system camera driver). Windows (Media Foundation) and Linux (udev
  rules) shouldn't - confirm.
- RealSense Viewer's "Post-Processing" toggle is librealsense's filter chain; the addon's
  spatial + temporal filters are the same filters, on by default (the viewer also decimates by 2).

## How to check a platform is done

Build all five examples from freshly generated projects, then with a camera connected:

1. `example-streams`: colour, depth, IR, colour-in-depth and the cloud live; the status line shows
   the camera, USB 3.x, librealsense 2.59.1, ~30 fps and the ms per frame.
2. `example-pointcloud`: textured cloud, the right way round (text in the scene reads correctly);
   "Point cloud faces" gives a mesh, "Colour-res point cloud" the colour-resolution grid.
3. `example-shader`: lit mesh (black mesh = normals or winding wrong).
4. `example-pointCloudTransform`: the cloud moves with the transform gizmo.
5. `example-playback`: `r` records, `r` again stops, `p` plays it back with the position moving,
   space pauses, left / right seek; a RealSense Viewer `.bag` dropped on the window plays.
6. Quit each with the window close button: clean exit, and the camera reopens straight away.

macOS reference (M3 Ultra, D415, `getCaptureStats().processMs`, all at the full frame rate):
points 7.8 ms (1280 x 720 depth, 1080p colour, spatial + temporal), depth-res mesh 11.5-13 ms,
colour-res points 12-14 ms, colour-res mesh 22-29 ms, filters off 4.3-5 ms, 848 x 480 @ 60 fps
3.2-3.6 ms. librealsense's own spatial filter alone: 9.8 ms; the addon's: 1.5-1.9 ms.

When a platform works, update its row in the main README and `scripts/README.md` ("not yet run"),
and commit the new `libs/librealsense2/lib/<platform>` files.
