# librealsense is built from source with scripts/build_librealsense_<platform> (see
# scripts/README.md) and installed into libs/librealsense2:
#
#   libs/librealsense2/include/librealsense2/      headers (the same on every platform)
#   libs/librealsense2/lib/osx/librealsense2.a    static, universal (arm64 + x86_64)
#   libs/librealsense2/lib/vs/x64/                realsense2.lib + realsense2.dll
#   libs/librealsense2/lib/linux64/               librealsense2.a (linuxaarch64 on ARM)
#
# librealsense's C++ API (rs.hpp) is header-only on top of its C API, so one Release build
# serves Debug and Release apps, and the static libraries export nothing but rs2_*.

meta:
	ADDON_NAME = ofxLibRealSense2
	ADDON_DESCRIPTION = Intel RealSense depth cameras (D400 / D500) through librealsense 2 - multiple cameras, threaded capture and point clouds / meshes, recording and playback. API shared with ofxKinectV2.
	ADDON_AUTHOR = YOSHIDA Shinya (original addon), Fred Rodrigues
	ADDON_TAGS = "realsense" "depth camera" "librealsense" "point cloud" "device"
	ADDON_URL = https://github.com/fred-dev/ofxLibRealSense2

common:

osx:
	# libusb inside librealsense2.a needs these.
	ADDON_FRAMEWORKS = IOKit CoreFoundation Security

vs:
	# realsense2.dll in lib/vs/x64 is copied next to the exe by the project generator.

linux64:
	ADDON_LDFLAGS = -ludev -lpthread

linuxaarch64:
	ADDON_LDFLAGS = -ludev -lpthread
