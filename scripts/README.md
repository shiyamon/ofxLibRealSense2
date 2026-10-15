# Building librealsense for ofxLibRealSense2

> Bringing the addon up on Windows or Linux? Read [HANDOFF_WINDOWS_LINUX.md](HANDOFF_WINDOWS_LINUX.md) first.

ofxLibRealSense2 links [librealsense](https://github.com/IntelRealSense/librealsense) built from
source with the scripts here, with the same options on every platform. Built and tested at
**v2.59.1** (`c84fe6438`). Clone it anywhere and pass its path to the script.

| | macOS | Windows | Linux |
|---|---|---|---|
| Library | static `librealsense2.a`, universal (arm64 + x86_64), macOS 11+ | `realsense2.dll` + `.lib` | static `librealsense2.a` |
| Backend | libusb (RSUSB, required on macOS) | Media Foundation (native) | libusb (`--v4l2` for the kernel backend) |
| SIMD | NEON (arm64), SSSE3 / AVX (x86_64) | SSSE3 / AVX | SSSE3 / AVX or NEON |
| CUDA | n/a | off (`-Cuda` to enable) | off |
| Recording | `.bag` (rosbag 1) | same | same |
| Off | examples, tools, tests, OpenMP, update checks, usage statistics, DDS, rosbag2 | same | same |
| Status | built and tested | not yet run | not yet run |

The C++ API (`rs.hpp`) is header-only over librealsense's C API, so:

- the static libraries pre-link librealsense with all it bundles (libusb, json, easylogging, lz4...)
  into one object that exports only `rs2_*`: nothing inside can clash with openFrameworks (which has
  its own libusb, json...) or other addons such as ofxKinectV2;
- one Release DLL serves Debug and Release apps on Windows.

Output:

```
libs/librealsense2/include/librealsense2/       headers (the same everywhere)
libs/librealsense2/lib/osx/librealsense2.a
libs/librealsense2/lib/vs/x64/realsense2.lib    + realsense2.dll (copied next to the exe)
libs/librealsense2/lib/linux64/librealsense2.a  (linuxaarch64 on ARM)
```

Then regenerate your project with the projectGenerator.

## macOS

```bash
brew install cmake
./build_librealsense_macos.sh /path/to/librealsense            # universal (what's in the repo)
./build_librealsense_macos.sh /path/to/librealsense --native   # this Mac's architecture only
```

Needs full Xcode. What the script takes care of:

- **Universal:** librealsense picks its SIMD code from `$CC -dumpmachine`, so each architecture is a
  separate build (the other one with compiler wrappers that target it), joined with `lipo`.
- **libusb** is librealsense's own static copy (`USE_EXTERNAL_USB=ON`), not Homebrew's dylib, built
  without `pipe2()`: the current SDK declares it but only macOS 27+ has it, and an unguarded call
  would crash older systems. Common symbols are turned off (`-fno-common`) so libusb's globals can
  be hidden too. Both through a toolchain file, which librealsense hands on to its libusb build.
- **One object:** `ld -r` over all of librealsense's static libraries plus libusb, exporting only
  `_rs2_*` (rsutils' duplicate copy of easylogging++ is dropped). librealsense's own
  `realsense2-all` bundle needs GNU `ar`, which macOS doesn't have.
- **rosbag2 off:** its yaml-cpp only arrives as headers in a static build. Recording and playback use
  the classic `.bag` format, which RealSense Viewer reads and writes too.

At run time librealsense needs **root** on macOS 12+ (see the main README).

## Windows

```powershell
.\build_librealsense_windows.ps1 -Source C:\path\to\librealsense
.\build_librealsense_windows.ps1 -Source C:\path\to\librealsense -Cuda
```

Needs Visual Studio 2022 or newer with the C++ workload. The native Media Foundation backend needs no
driver changes or admin rights (unlike the Kinect v2, no Zadig). The C runtime is linked statically,
so `realsense2.dll` is all there is to ship.

**CUDA** is off by default. librealsense would only use it for colour format conversion: the addon
does registration, alignment, filtering and point clouds itself, multi-threaded. And a CUDA build
links cuBLAS and cuSPARSE, hundreds of MB of DLLs every app would have to ship. With `-Cuda`, sm_52
and sm_61 (GTX 9xx / 10xx, e.g. the 1080 Ti) are added: librealsense's own list leaves them out.

## Linux

```bash
sudo apt install build-essential cmake git pkg-config libudev-dev libssl-dev
./build_librealsense_linux.sh /path/to/librealsense
sudo cp /path/to/librealsense/config/99-realsense-libusb.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The libusb backend needs no kernel patches; the udev rules let users open the cameras without root.
`--v4l2` builds the kernel backend instead (frame metadata then needs librealsense's kernel
patches). Same pre-linking as macOS with GNU tools (`ld -r -d`, `objcopy --keep-global-symbol`).
The app links `-ludev -lpthread` (`addon_config.mk`).

## Testing a build

librealsense's own tools aren't built. A quick check without openFrameworks: build any program
against the library (see the frameworks in `addon_config.mk`) and list the devices - or just run
`example-streams` (with `sudo` on macOS).
