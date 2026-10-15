//
//  ofxLibRealSense2.cpp
//

#include "ofxLibRealSense2.h"
#include "ofxLibRealSense2Processing.h"
#include "ofGLProgrammableRenderer.h"

#include <librealsense2/rsutil.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <thread>
#ifdef TARGET_OSX
#include <unistd.h>
#endif

std::atomic<int> ofxLibRealSense2::openCameraCount{ 0 };

namespace
{
    std::mutex contextMutex;
    std::weak_ptr<rs2::context> sharedContext;
    // Serials of cameras unplugged since they were opened, from the context's devices-changed
    // callback (polling the device list instead would try to open cameras that are in use).
    std::mutex removedMutex;
    std::set<std::string> removedSerials;
    std::map<std::string, rs2::device> openDevices;

    void trackDevice(const std::string& serial, const rs2::device& device)
    {
        std::lock_guard<std::mutex> lock(removedMutex);
        removedSerials.erase(serial);
        if (device) openDevices[serial] = device;
        else openDevices.erase(serial);
    }

    bool wasRemoved(const std::string& serial)
    {
        std::lock_guard<std::mutex> lock(removedMutex);
        return removedSerials.count(serial) > 0;
    }

    void swapFrames(ofxLibRealSense2::Frames& a, ofxLibRealSense2::Frames& b)
    {
        a.color.swap(b.color);
        a.registered.swap(b.registered);
        a.depth.swap(b.depth);
        a.ir.swap(b.ir);
        a.bigDepth.swap(b.bigDepth);
        a.colorMap.swap(b.colorMap);
        std::swap(a.hasColor, b.hasColor);
        std::swap(a.hasDepth, b.hasDepth);
        std::swap(a.hasIr, b.hasIr);
        std::swap(a.hasRegistered, b.hasRegistered);
        std::swap(a.hasBigDepth, b.hasBigDepth);
        std::swap(a.frameNumber, b.frameNumber);
        std::swap(a.timestampMs, b.timestampMs);
        std::swap(a.depthIntrinsics, b.depthIntrinsics);
        std::swap(a.colorIntrinsics, b.colorIntrinsics);
        std::swap(a.mirrored, b.mirrored);
    }

    void swapCloud(ofxLibRealSense2::Cloud& a, ofxLibRealSense2::Cloud& b)
    {
        a.positions.swap(b.positions);
        a.normals.swap(b.normals);
        a.texCoords.swap(b.texCoords);
        a.colors.swap(b.colors);
        a.indices.swap(b.indices);
    }

    using ofxLibRealSense2Processing::parallelRows;

    bool sameIntrinsics(const rs2_intrinsics& a, const rs2_intrinsics& b)
    {
        return std::memcmp(&a, &b, sizeof(rs2_intrinsics)) == 0;
    }

    // Camera space ray (x / z, y / z) through every pixel of a camera, lens distortion
    // included. Deprojecting through some distortion models iterates, so the table is built
    // once per calibration and kept per thread (each camera's capture thread has its own).
    const std::vector<glm::vec2>& raysFor(const rs2_intrinsics& in, int threads)
    {
        struct Cache { rs2_intrinsics intrinsics{}; std::vector<glm::vec2> rays; };
        thread_local Cache cache[2];
        thread_local int next = 0;
        for (auto& c : cache)
        {
            if (!c.rays.empty() && sameIntrinsics(c.intrinsics, in)) return c.rays;
        }
        Cache& c = cache[next];
        next = 1 - next;
        c.intrinsics = in;
        c.rays.resize(static_cast<size_t>(in.width) * in.height);
        parallelRows(in.height, threads, [&](int, int y0, int y1) {
            for (int y = y0; y < y1; ++y)
            {
                for (int x = 0; x < in.width; ++x)
                {
                    float p[3];
                    const float pixel[2] = { static_cast<float>(x), static_cast<float>(y) };
                    rs2_deproject_pixel_to_point(p, &in, pixel, 1.f);
                    c.rays[static_cast<size_t>(y) * in.width + x] = glm::vec2(p[0], p[1]);
                }
            }
        });
        return c.rays;
    }

    // Copies an image, flipping it horizontally on the way when asked: one pass instead of a
    // copy followed by a flip. P is one whole pixel (e.g. uint32_t for RGBA).
    template<typename P, typename T>
    void copyImage(const void* src, int w, int h, int strideBytes, ofPixelFormat format, bool flip, ofPixels_<T>& dst)
    {
        if (dst.getWidth() != static_cast<size_t>(w) || dst.getHeight() != static_cast<size_t>(h) || dst.getPixelFormat() != format)
        {
            dst.allocate(w, h, format);
        }
        P* out = reinterpret_cast<P*>(dst.getData());
        for (int y = 0; y < h; ++y)
        {
            const P* in = reinterpret_cast<const P*>(static_cast<const uint8_t*>(src) + static_cast<size_t>(y) * strideBytes);
            if (flip) std::reverse_copy(in, in + w, out + static_cast<size_t>(y) * w);
            else std::copy(in, in + w, out + static_cast<size_t>(y) * w);
        }
    }

    // Edge-preserving 3x3 average of depth (mm) around one pixel, with neighbours `spacing`
    // pixels apart. Only neighbours on the same surface (within a depth-relative threshold)
    // are averaged, so silhouettes stay sharp, and holes are never filled.
    inline float denoisedDepthAt(const float* depth, int w, int h, int x, int y, int spacing)
    {
        const float center = depth[y * w + x];
        if (!(center > 0.f && center < 1e6f)) return center;

        const float threshold = std::max(15.f, center * 0.015f);
        float sum = 0.f;
        int count = 0;
        for (int dy = -spacing; dy <= spacing; dy += spacing)
        {
            const int yy = y + dy;
            if (yy < 0 || yy >= h) continue;
            const float* row = depth + yy * w;
            for (int dx = -spacing; dx <= spacing; dx += spacing)
            {
                const int xx = x + dx;
                if (xx < 0 || xx >= w) continue;
                const float n = row[xx];
                if (n > 0.f && std::abs(n - center) < threshold)
                {
                    sum += n;
                    ++count;
                }
            }
        }
        return sum / count;
    }

    // Retrying a failed open only helps when the process may claim the camera at all: on
    // macOS that needs root (without it every attempt fails the same way).
    bool isTransientAvailable()
    {
#ifdef TARGET_OSX
        return geteuid() == 0;
#else
        return true;
#endif
    }

    std::string describeOpenError(const std::string& what)
    {
#ifdef TARGET_OSX
        // librealsense uses libusb on macOS, and since macOS 12 the system's camera driver
        // keeps the camera's video interfaces unless the process is root.
        if (what.find("access") != std::string::npos || what.find("busy") != std::string::npos)
        {
            return what + " - on macOS 12+ librealsense needs root: run the app with sudo (see README)";
        }
#endif
        return what;
    }
}

ofxLibRealSense2::ofxLibRealSense2()
{
    params.setName("realsense2");

    // Metres, like ofxKinectV2 / ofxK4a. Points outside the range are dropped from the point
    // cloud and shown black in the depth image.
    params.add(minDistance.set("Min distance (m)", 0.2f, 0.f, 10.f));
    params.add(maxDistance.set("Max distance (m)", 6.f, 0.f, 10.f));

    params.add(pointCloudHasFaces.set("Point cloud faces", false));
    // Colour image samples in colour camera space instead of depth image samples in depth
    // camera space.
    params.add(bigPointCloud.set("Colour-res point cloud", false));
    // Triangles are only kept when every edge is shorter than this (scaled by the step)...
    params.add(facesMaxLength.set("Max face edge (m)", 0.05f, 0.001f, 0.5f));
    // ...and the depth difference between corners stays under this fraction of the distance
    // (per step). Depth noise and pixel footprint both grow with distance, so a relative
    // limit separates foreground from background evenly at any range.
    params.add(facesMaxDepthJump.set("Max depth jump", 0.03f, 0.001f, 0.2f));
    params.add(steps.set("Point cloud step", 1, 1, 10));
    params.add(denoiseDepth.set("Denoise depth", false));
    params.add(pointCloudAlpha.set("Point cloud alpha", 255, 0, 255));

    params.add(decimation.set("Decimation", 1, 1, 8));
    params.add(spatialFilter.set("Spatial filter", true));
    params.add(temporalFilter.set("Temporal filter", true));
    params.add(holeFilling.set("Hole filling", false));

    // Device options: ranges and current values come from the camera in open().
    params.add(emitter.set("Emitter", true));
    params.add(laserPower.set("Laser power", 150.f, 0.f, 360.f));
    params.add(depthAutoExposure.set("Depth auto exposure", true));
    params.add(depthExposure.set("Depth exposure (us)", 8500.f, 1.f, 165000.f));
    params.add(autoExposure.set("auto exposure", true));
    params.add(exposure.set("Exposure", 156.f, 1.f, 10000.f));
    params.add(gain.set("Gain", 64.f, 0.f, 128.f));
    params.add(autoWhiteBalance.set("auto white balance", true));
    params.add(whiteBalance.set("White balance (K)", 4600.f, 2800.f, 6500.f));

    optionListeners.push(emitter.newListener([this](bool& on) { setSensorOption(false, RS2_OPTION_EMITTER_ENABLED, on ? 1.f : 0.f); }));
    optionListeners.push(laserPower.newListener([this](float& v) { setSensorOption(false, RS2_OPTION_LASER_POWER, v); }));
    optionListeners.push(depthAutoExposure.newListener([this](bool& on) {
        setSensorOption(false, RS2_OPTION_ENABLE_AUTO_EXPOSURE, on ? 1.f : 0.f);
        if (!on) setSensorOption(false, RS2_OPTION_EXPOSURE, depthExposure);
    }));
    optionListeners.push(depthExposure.newListener([this](float& v) {
        if (depthAutoExposure) depthAutoExposure = false; // its listener applies the exposure
        else setSensorOption(false, RS2_OPTION_EXPOSURE, v);
    }));
    optionListeners.push(autoExposure.newListener([this](bool& on) {
        setSensorOption(true, RS2_OPTION_ENABLE_AUTO_EXPOSURE, on ? 1.f : 0.f);
        if (!on)
        {
            setSensorOption(true, RS2_OPTION_EXPOSURE, exposure);
            setSensorOption(true, RS2_OPTION_GAIN, gain);
        }
    }));
    optionListeners.push(exposure.newListener([this](float& v) {
        if (autoExposure) autoExposure = false;
        else setSensorOption(true, RS2_OPTION_EXPOSURE, v);
    }));
    optionListeners.push(gain.newListener([this](float& v) {
        if (autoExposure) autoExposure = false;
        else setSensorOption(true, RS2_OPTION_GAIN, v);
    }));
    optionListeners.push(autoWhiteBalance.newListener([this](bool& on) {
        setSensorOption(true, RS2_OPTION_ENABLE_AUTO_WHITE_BALANCE, on ? 1.f : 0.f);
        if (!on) setSensorOption(true, RS2_OPTION_WHITE_BALANCE, whiteBalance);
    }));
    optionListeners.push(whiteBalance.newListener([this](float& v) {
        if (autoWhiteBalance) autoWhiteBalance = false;
        else setSensorOption(true, RS2_OPTION_WHITE_BALANCE, v);
    }));

    pointCloud.setMode(OF_PRIMITIVE_POINTS);
    pointCloud.setUsage(GL_STREAM_DRAW);
}

ofxLibRealSense2::~ofxLibRealSense2()
{
    close();
}

std::shared_ptr<rs2::context> ofxLibRealSense2::getContext()
{
    std::lock_guard<std::mutex> lock(contextMutex);
    auto context = sharedContext.lock();
    if (!context)
    {
        // librealsense's own warnings are mostly benign retries (e.g. control transfers timing
        // out on macOS): only its errors by default, everything with OF_LOG_VERBOSE.
        rs2::log_to_console(ofGetLogLevel() == OF_LOG_VERBOSE ? RS2_LOG_SEVERITY_WARN : RS2_LOG_SEVERITY_ERROR);
        context = std::make_shared<rs2::context>();
        // Only was_removed() here: listing the new devices would make librealsense open them,
        // racing any camera being opened at the same time. A camera that comes back clears
        // its flag when its frames do.
        context->set_devices_changed_callback([](rs2::event_information& info) {
            std::lock_guard<std::mutex> lock(removedMutex);
            for (auto& open : openDevices)
            {
                if (info.was_removed(open.second)) removedSerials.insert(open.first);
            }
        });
        sharedContext = context;
    }
    return context;
}

std::string ofxLibRealSense2::getLibraryVersion()
{
    const int v = rs2_get_api_version(nullptr);
    return ofToString(v / 10000) + "." + ofToString((v / 100) % 100) + "." + ofToString(v % 100);
}

std::vector<ofxLibRealSense2::DeviceInfo> ofxLibRealSense2::listDevices()
{
    std::vector<DeviceInfo> devices;
    try
    {
        for (auto&& dev : getContext()->query_devices())
        {
            auto info = [&](rs2_camera_info i) { return dev.supports(i) ? std::string(dev.get_info(i)) : std::string(); };
            DeviceInfo d;
            d.serial = info(RS2_CAMERA_INFO_SERIAL_NUMBER);
            d.name = info(RS2_CAMERA_INFO_NAME);
            d.usbType = info(RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR);
            d.firmware = info(RS2_CAMERA_INFO_FIRMWARE_VERSION);
            d.deviceId = 0;
            devices.push_back(d);
        }
    }
    catch (const rs2::error& e)
    {
        ofLogError("ofxLibRealSense2::listDevices") << e.what();
    }

    ofSort(devices, [](const DeviceInfo& a, const DeviceInfo& b) { return a.serial < b.serial; });
    for (std::size_t i = 0; i < devices.size(); i++)
    {
        devices[i].deviceId = static_cast<int>(i);
    }
    return devices;
}

std::size_t ofxLibRealSense2::getDeviceCount()
{
    return listDevices().size();
}

bool ofxLibRealSense2::open(int deviceId, const Settings& s)
{
    if (!s.playbackFile.empty()) return open(std::string(), s);

    const auto devices = listDevices();
    if (devices.empty())
    {
        ofLogError("ofxLibRealSense2::open") << "no RealSense cameras connected";
        return false;
    }
    if (deviceId < 0 || deviceId >= static_cast<int>(devices.size()))
    {
        ofLogError("ofxLibRealSense2::open") << "deviceId " << deviceId << " is out of range, " << devices.size() << " camera(s) connected";
        return false;
    }
    return open(devices[deviceId].serial, s);
}

bool ofxLibRealSense2::open(const std::string& serial)
{
    return open(serial, Settings());
}

bool ofxLibRealSense2::open(int deviceId)
{
    return open(deviceId, Settings());
}

bool ofxLibRealSense2::openFile(const std::string& path, const Settings& s)
{
    Settings withFile = s;
    withFile.playbackFile = path;
    return open(std::string(), withFile);
}

bool ofxLibRealSense2::openFile(const std::string& path)
{
    return openFile(path, Settings());
}

bool ofxLibRealSense2::open(const std::string& requestedSerial, const Settings& s)
{
    close();

    settings = s;
    // Registration needs both cameras; IR and the point cloud come from the depth stream.
    if (settings.usePointCloud) settings.enableDepth = true;
    if (settings.registerImages && !(settings.enableRGB && settings.enableDepth)) settings.registerImages = false;
    if (!settings.enableRGB && !settings.enableDepth && !settings.enableIR)
    {
        ofLogError("ofxLibRealSense2::open") << "nothing to stream: colour, depth and IR are all disabled";
        return false;
    }

    context = getContext();
    // A camera that was just closed (here or by another app) re-enumerates, and for a moment
    // the system's camera driver holds it again: librealsense then fails to claim it ("failed
    // to set power state"). Retry those for a couple of seconds.
    const int attempts = isTransientAvailable() ? 5 : 1;
    for (int attempt = 1;; ++attempt)
    {
        try
        {
            rs2::config cfg;
            rs2::device device;
            if (!settings.playbackFile.empty())
            {
                const std::string path = ofToDataPath(settings.playbackFile, true);
                if (!ofFile::doesFileExist(path))
                {
                    ofLogError("ofxLibRealSense2::open") << "recording not found: " << path;
                    return false;
                }
                settings.playbackFile = path;
                cfg.enable_device_from_file(path, settings.playbackLoop);
            }
            else
            {
                for (auto&& d : context->query_devices())
                {
                    if (d.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER) == requestedSerial) device = d;
                }
                if (!device)
                {
                    ofLogError("ofxLibRealSense2::open") << "no camera with serial " << requestedSerial;
                    return false;
                }
                cfg.enable_device(requestedSerial);
                if (settings.syncMode != SyncMode::DEFAULT)
                {
                    auto depthSensor = device.first<rs2::depth_sensor>();
                    if (depthSensor.supports(RS2_OPTION_INTER_CAM_SYNC_MODE))
                    {
                        depthSensor.set_option(RS2_OPTION_INTER_CAM_SYNC_MODE, static_cast<float>(settings.syncMode));
                    }
                    else
                    {
                        ofLogWarning("ofxLibRealSense2::open") << requestedSerial << " doesn't support hardware sync";
                    }
                }
                if (!settings.recordFile.empty())
                {
                    cfg.enable_record_to_file(ofToDataPath(settings.recordFile, true));
                }
            }

            const int fps = settings.fps;
            if (settings.enableDepth) cfg.enable_stream(RS2_STREAM_DEPTH, settings.depthWidth, settings.depthHeight, RS2_FORMAT_Z16, fps);
            if (settings.enableIR) cfg.enable_stream(RS2_STREAM_INFRARED, 1, settings.depthWidth, settings.depthHeight, RS2_FORMAT_Y8, fps);
            // Each attempt starts from what was asked for.
            bool colour = s.enableRGB;
            if (colour) cfg.enable_stream(RS2_STREAM_COLOR, settings.colorWidth, settings.colorHeight, RS2_FORMAT_RGBA8, fps);
            pipeline = rs2::pipeline(*context);
            if (colour && !cfg.can_resolve(pipeline))
            {
                // A busy camera doesn't resolve either: only give up on colour at the last try.
                if (attempt < attempts)
                {
                    ofSleepMillis(500);
                    continue;
                }
                // e.g. a camera or recording without colour: stream the rest
                ofLogWarning("ofxLibRealSense2::open") << "no colour stream with these settings, opening without colour";
                cfg.disable_stream(RS2_STREAM_COLOR);
                colour = false;
            }
            profile = pipeline.start(cfg);
            settings.enableRGB = colour;
            if (!colour) settings.registerImages = false;
            break;
        }
        catch (const rs2::error& e)
        {
            pipeline = rs2::pipeline();
            profile = rs2::pipeline_profile();
            const std::string what = e.what();
            const bool transient = settings.playbackFile.empty() &&
                (what.find("power state") != std::string::npos || what.find("claim") != std::string::npos || what.find("busy") != std::string::npos);
            if (transient && attempt < attempts)
            {
                ofLogNotice("ofxLibRealSense2::open") << requestedSerial << " busy (" << what << "), retrying";
                ofSleepMillis(500);
                continue;
            }
            ofLogError("ofxLibRealSense2::open") << "could not open " << (settings.playbackFile.empty() ? requestedSerial : settings.playbackFile)
                                              << ": " << describeOpenError(what);
            return false;
        }
    }

    auto device = profile.get_device();
    serial = device.supports(RS2_CAMERA_INFO_SERIAL_NUMBER) ? device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER) : requestedSerial;
    name = device.supports(RS2_CAMERA_INFO_NAME) ? device.get_info(RS2_CAMERA_INFO_NAME) : "RealSense";
    if (auto playback = device.as<rs2::playback>()) playback.set_real_time(settings.playbackRealtime);
    else trackDevice(serial, device);

    {
        std::lock_guard<std::mutex> lock(calibrationMutex);
        depthIntrinsics = rs2_intrinsics{};
        colorIntrinsics = rs2_intrinsics{};
        depthToColor = rs2_extrinsics{ { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, 0, 0 } };
        rs2::stream_profile depthProfile, colorProfile;
        for (auto&& sp : profile.get_streams())
        {
            if (sp.stream_type() == RS2_STREAM_DEPTH) depthProfile = sp;
            if (sp.stream_type() == RS2_STREAM_COLOR) colorProfile = sp;
        }
        if (depthProfile) depthIntrinsics = depthProfile.as<rs2::video_stream_profile>().get_intrinsics();
        if (colorProfile) colorIntrinsics = colorProfile.as<rs2::video_stream_profile>().get_intrinsics();
        if (depthProfile && colorProfile) depthToColor = depthProfile.get_extrinsics_to(colorProfile);
        depthScale = 0.001f;
        if (auto ds = device.first<rs2::depth_sensor>()) depthScale = ds.get_depth_scale();
    }

    std::string streams;
    for (auto&& sp : profile.get_streams())
    {
        auto v = sp.as<rs2::video_stream_profile>();
        streams += " " + sp.stream_name() + " " + ofToString(v.width()) + "x" + ofToString(v.height()) + "@" + ofToString(sp.fps());
    }
    const std::string usb = device.supports(RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR) ? device.get_info(RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR) : "";
    stats = CaptureStats();
    stats.pipeline = name + (usb.empty() ? "" : " USB " + usb) + (isPlayback() ? " (playback)" : "");
    ofLogNotice("ofxLibRealSense2::open") << "opened " << name << " " << serial << (usb.empty() ? "" : " on USB " + usb) << ":" << streams
                                       << ", librealsense " << getLibraryVersion();
    if (!usb.empty() && usb[0] == '2')
    {
        ofLogWarning("ofxLibRealSense2::open") << serial << " is on USB 2: plug it into a USB 3 port (and cable) for full resolution and frame rate";
    }

    params.setName("RealSense " + serial);
    pointCloudHasFaces = settings.pointCloudHasFaces;
    bigPointCloud = settings.bigPointCloud;
    minDistance = settings.minDepth;
    maxDistance = settings.maxDepth;
    decimation = settings.decimation;
    spatialFilter = settings.spatialFilter;
    temporalFilter = settings.temporalFilter;
    holeFilling = settings.holeFilling;

    lastColorFrame = lastDepthFrame = dropped = 0;
    bNewFrame = false;
    bNewBuffer = false;
    lastFrameNo = std::numeric_limits<uint64_t>::max();
    bOpened = true;
    setupOptionParams();
    openCameraCount++;
    startThread();
    return true;
}

void ofxLibRealSense2::setupOptionParams()
{
    // The parameters are the source of truth, as in ofxKinectV2: their ranges come from the
    // camera, then their values are applied to it (a camera can be left in any state by
    // another app, e.g. with the laser off). Loading a settings file afterwards applies
    // the saved values the same way.
    auto device = profile.get_device();
    rs2::sensor depthSensor, colorSensor;
    for (auto&& s : device.query_sensors())
    {
        if (s.is<rs2::depth_sensor>() && !depthSensor) depthSensor = s;
        if (s.is<rs2::color_sensor>() && !colorSensor) colorSensor = s;
    }
    auto range = [](rs2::sensor& sensor, rs2_option option, auto& param) {
        if (!sensor || !sensor.supports(option)) return;
        try
        {
            const auto r = sensor.get_option_range(option);
            using T = typename std::decay<decltype(param.get())>::type;
            param.setMin(static_cast<T>(r.min));
            param.setMax(static_cast<T>(r.max));
            param.setWithoutEventNotifications(ofClamp(param.get(), param.getMin(), param.getMax()));
        }
        catch (const rs2::error&) {}
    };
    range(depthSensor, RS2_OPTION_LASER_POWER, laserPower);
    range(depthSensor, RS2_OPTION_EXPOSURE, depthExposure);
    range(colorSensor, RS2_OPTION_EXPOSURE, exposure);
    range(colorSensor, RS2_OPTION_GAIN, gain);
    range(colorSensor, RS2_OPTION_WHITE_BALANCE, whiteBalance);

    if (isPlayback()) return;
    setSensorOption(false, RS2_OPTION_EMITTER_ENABLED, emitter ? 1.f : 0.f);
    setSensorOption(false, RS2_OPTION_LASER_POWER, laserPower);
    setSensorOption(false, RS2_OPTION_ENABLE_AUTO_EXPOSURE, depthAutoExposure ? 1.f : 0.f);
    if (!depthAutoExposure) setSensorOption(false, RS2_OPTION_EXPOSURE, depthExposure);
    setSensorOption(true, RS2_OPTION_ENABLE_AUTO_EXPOSURE, autoExposure ? 1.f : 0.f);
    if (!autoExposure)
    {
        setSensorOption(true, RS2_OPTION_EXPOSURE, exposure);
        setSensorOption(true, RS2_OPTION_GAIN, gain);
    }
    setSensorOption(true, RS2_OPTION_ENABLE_AUTO_WHITE_BALANCE, autoWhiteBalance ? 1.f : 0.f);
    if (!autoWhiteBalance) setSensorOption(true, RS2_OPTION_WHITE_BALANCE, whiteBalance);
}

void ofxLibRealSense2::setSensorOption(bool colour, rs2_option option, float value)
{
    if (!bOpened || isPlayback()) return;
    std::lock_guard<std::mutex> lock(optionMutex);
    try
    {
        for (auto&& s : profile.get_device().query_sensors())
        {
            if (colour ? !s.is<rs2::color_sensor>() : !s.is<rs2::depth_sensor>()) continue;
            if (!s.supports(option) || s.is_option_read_only(option)) return;
            const auto range = s.get_option_range(option);
            // options with a step (e.g. white balance in 10 K) reject values off the grid
            float v = ofClamp(value, range.min, range.max);
            if (range.step > 0) v = range.min + std::round((v - range.min) / range.step) * range.step;
            s.set_option(option, v);
            return;
        }
    }
    catch (const rs2::error& e)
    {
        ofLogWarning("ofxLibRealSense2") << serial << ": could not set " << rs2_option_to_string(option) << " to " << value << ": " << e.what();
    }
}

ofxLibRealSense2::CloudOptions ofxLibRealSense2::getCloudOptions() const
{
    CloudOptions o;
    o.minDistance = minDistance;
    o.maxDistance = maxDistance;
    o.faces = pointCloudHasFaces;
    o.facesMaxLength = facesMaxLength;
    o.facesMaxDepthJump = facesMaxDepthJump;
    o.steps = std::max(1, steps.get());
    o.bigPointCloud = bigPointCloud && settings.enableRGB;
    o.texCoords = settings.pointCloudTexCoords;
    o.denoise = denoiseDepth;
    o.unmirror = settings.unmirror;
    o.alpha = pointCloudAlpha / 255.f;
    o.transform = bTransformPointCloud;
    if (o.transform)
    {
        std::lock_guard<std::mutex> lock(transformMutex);
        o.transformation = pointCloudTransformationMat;
    }
    return o;
}

bool ofxLibRealSense2::processFrames(const rs2::frameset& input, Frames& out)
{
    const bool flip = !settings.unmirror;
    const bool wantBigDepth = settings.usePointCloud && bigPointCloud && settings.enableRGB;
    const int threads = getBuildThreadCount();

    // librealsense's post-processing, in the order Intel recommends. The filters pass the
    // other streams of the frame set through untouched.
    rs2::frameset fs = input;
    if (fs.get_depth_frame())
    {
        const int dec = ofClamp(decimation.get(), 1, 8);
        if (dec > 1)
        {
            decimationBlock.set_option(RS2_OPTION_FILTER_MAGNITUDE, static_cast<float>(dec));
            fs = fs.apply_filter(decimationBlock);
        }
        if (spatialFilter || temporalFilter)
        {
            fs = fs.apply_filter(toDisparity);
            if (spatialFilter)
            {
                // librealsense's spatial filter, multi-threaded (same output). The disparity
                // frame was just made for us by toDisparity, so it's filtered in place.
                if (auto disparity = fs.get_depth_frame())
                {
                    ofxLibRealSense2Processing::spatialFilter(static_cast<float*>(const_cast<void*>(disparity.get_data())),
                        disparity.get_width(), disparity.get_height(),
                        spatialBlock.get_option(RS2_OPTION_FILTER_SMOOTH_ALPHA),
                        spatialBlock.get_option(RS2_OPTION_FILTER_SMOOTH_DELTA),
                        static_cast<int>(spatialBlock.get_option(RS2_OPTION_FILTER_MAGNITUDE)), threads);
                }
            }
            if (temporalFilter) fs = fs.apply_filter(temporalBlock);
            fs = fs.apply_filter(toDepth);
        }
        if (holeFilling) fs = fs.apply_filter(holeFillingBlock);
    }

    rs2::video_frame color = fs.get_color_frame();
    rs2::depth_frame depth = fs.get_depth_frame();
    rs2::video_frame ir = fs.get_infrared_frame(1);
    if (!ir) ir = fs.get_infrared_frame();

    out.hasColor = bool(color);
    out.hasDepth = bool(depth);
    out.hasIr = bool(ir);
    out.hasRegistered = out.hasColor && out.hasDepth && settings.registerImages;
    out.hasBigDepth = false;
    out.mirrored = flip;
    out.frameNumber = depth ? depth.get_frame_number() : (color ? color.get_frame_number() : 0);
    out.timestampMs = depth ? depth.get_timestamp() : (color ? color.get_timestamp() : 0);

    // Frames lost on the way (gaps in the counters) and colour / depth timing.
    auto countGap = [&](uint64_t number, uint64_t& last) {
        if (last > 0 && number > last + 1) dropped += number - last - 1;
        last = number;
    };
    if (depth) countGap(depth.get_frame_number(), lastDepthFrame);
    if (color) countGap(color.get_frame_number(), lastColorFrame);

    if (color)
    {
        out.colorIntrinsics = color.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
        copyImage<uint32_t>(color.get_data(), color.get_width(), color.get_height(), color.get_stride_in_bytes(), OF_PIXELS_RGBA, flip, out.color);
    }
    if (ir)
    {
        const int w = ir.get_width(), h = ir.get_height();
        out.ir.allocate(w, h, OF_PIXELS_GRAY);
        const uint8_t* src = static_cast<const uint8_t*>(ir.get_data());
        float* dst = out.ir.getData();
        for (int y = 0; y < h; ++y)
        {
            const uint8_t* row = src + static_cast<size_t>(y) * ir.get_stride_in_bytes();
            for (int x = 0; x < w; ++x) dst[y * w + x] = row[flip ? w - 1 - x : x];
        }
    }
    if (depth)
    {
        out.depthIntrinsics = depth.get_profile().as<rs2::video_stream_profile>().get_intrinsics();
        const int w = depth.get_width(), h = depth.get_height();
        out.depth.allocate(w, h, OF_PIXELS_GRAY);
        const float toMM = depth.get_units() * 1000.f;
        const uint16_t* src = static_cast<const uint16_t*>(depth.get_data());
        const int stride = depth.get_stride_in_bytes() / 2;
        float* dst = out.depth.getData();
        parallelRows(h, threads, [&](int, int y0, int y1) {
            for (int y = y0; y < y1; ++y)
            {
                const uint16_t* row = src + static_cast<size_t>(y) * stride;
                float* o = dst + static_cast<size_t>(y) * w;
                for (int x = 0; x < w; ++x) o[x] = row[flip ? w - 1 - x : x] * toMM;
            }
        });
    }

    if (wantBigDepth && color && depth)
    {
        // Depth re-projected into the colour camera, z-buffered so nearer surfaces win:
        // librealsense's align, multi-threaded.
        rs2_extrinsics extrinsics;
        {
            std::lock_guard<std::mutex> lock(calibrationMutex);
            extrinsics = depthToColor;
        }
        const int w = out.colorIntrinsics.width, h = out.colorIntrinsics.height;
        bigDepthRaw.resize(static_cast<size_t>(w) * h);
        ofxLibRealSense2Processing::alignDepthToOther(static_cast<const uint16_t*>(depth.get_data()), out.depthIntrinsics, extrinsics,
                                                     out.colorIntrinsics, depth.get_units(), bigDepthRaw.data(), threads, alignScratch);
        out.bigDepth.allocate(w, h, OF_PIXELS_GRAY);
        const float toMM = depth.get_units() * 1000.f;
        float* dst = out.bigDepth.getData();
        parallelRows(h, threads, [&](int, int y0, int y1) {
            for (int y = y0; y < y1; ++y)
            {
                const uint16_t* row = bigDepthRaw.data() + static_cast<size_t>(y) * w;
                float* o = dst + static_cast<size_t>(y) * w;
                for (int x = 0; x < w; ++x) o[x] = row[flip ? w - 1 - x : x] * toMM;
            }
        });
        out.hasBigDepth = true;
    }

    // Depth -> colour map: each depth pixel through the cameras' calibration into the colour
    // image. Gives the registered image and the depth-resolution cloud's colours.
    if (color && depth && (settings.registerImages || settings.usePointCloud))
    {
        const int w = static_cast<int>(out.depth.getWidth());
        const int h = static_cast<int>(out.depth.getHeight());
        const int cw = static_cast<int>(out.color.getWidth());
        const int ch = static_cast<int>(out.color.getHeight());
        rs2_extrinsics extrinsics;
        {
            std::lock_guard<std::mutex> lock(calibrationMutex);
            extrinsics = depthToColor;
        }
        const rs2_intrinsics colorIn = out.colorIntrinsics;
        const auto& rays = raysFor(out.depthIntrinsics, threads);
        const float* d = out.depth.getData();
        out.colorMap.resize(static_cast<size_t>(w) * h);
        int* map = out.colorMap.data();

        if (out.hasRegistered) out.registered.allocate(w, h, OF_PIXELS_RGBA);
        const uint32_t* colorPx = reinterpret_cast<const uint32_t*>(out.color.getData());
        uint32_t* reg = out.hasRegistered ? reinterpret_cast<uint32_t*>(out.registered.getData()) : nullptr;

        parallelRows(h, threads, [&](int, int y0, int y1) {
            for (int y = y0; y < y1; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const size_t i = static_cast<size_t>(y) * w + x;
                    const float z = d[i] * 0.001f;
                    int c = -1;
                    if (z > 0.f)
                    {
                        // stored images may be flipped: calibration is in the camera's own columns
                        const int sx = flip ? w - 1 - x : x;
                        const glm::vec2 r = rays[static_cast<size_t>(y) * w + sx];
                        const float p[3] = { r.x * z, r.y * z, z };
                        float q[3], px[2];
                        rs2_transform_point_to_point(q, &extrinsics, p);
                        rs2_project_point_to_pixel(px, &colorIn, q);
                        const int cx = static_cast<int>(px[0] + 0.5f);
                        const int cy = static_cast<int>(px[1] + 0.5f);
                        if (cx >= 0 && cy >= 0 && cx < cw && cy < ch) c = cy * cw + (flip ? cw - 1 - cx : cx);
                    }
                    map[i] = c;
                    if (reg) reg[i] = c >= 0 ? colorPx[c] : 0u;
                }
            }
        });
    }
    else
    {
        out.colorMap.clear();
    }
    return out.hasColor || out.hasDepth || out.hasIr;
}

void ofxLibRealSense2::threadedFunction()
{
    Frames back;
    ofPixels depthBack;
    ofPixels irBack;
    Cloud cloudBack;
    bool cloudInColourSpace = false;
    float fpsAverage = 0.f;
    float processAverage = 0.f;
    float buildAverage = 0.f;
    uint64_t lastFrameTime = 0;

    while (isThreadRunning())
    {
        rs2::frameset fs;
        std::string error;
        bool got = false;
        try
        {
            // A short timeout so close() never waits long on a stalled camera.
            got = pipeline.try_wait_for_frames(&fs, 500);
        }
        catch (const rs2::error& e)
        {
            error = e.what();
        }
        if (!got)
        {
            // Nothing for half a second: is the camera still there?
            const bool connected = isPlayback() || !wasRemoved(serial);
            lock();
            stats.connected = connected;
            if (!error.empty()) stats.lastError = error;
            else if (!connected) stats.lastError = "disconnected";
            stats.fps = 0.f;
            unlock();
            continue;
        }

        const uint64_t start = ofGetElapsedTimeMicros();
        const CloudOptions options = getCloudOptions();
        try
        {
            if (!processFrames(fs, back)) continue;
        }
        catch (const rs2::error& e)
        {
            lock();
            stats.lastError = e.what();
            unlock();
            continue;
        }

        if (back.hasDepth)
        {
            // Depth (mm) mapped into 0-255 over the point cloud range, near = bright; holes
            // and anything out of range stay black.
            const float minMM = options.minDistance * 1000.f;
            const float maxMM = options.maxDistance * 1000.f;
            const float scale = 254.f / std::max(1.f, maxMM - minMM);
            depthBack.allocate(back.depth.getWidth(), back.depth.getHeight(), OF_PIXELS_GRAY);
            const float* d = back.depth.getData();
            unsigned char* out = depthBack.getData();
            parallelRows(static_cast<int>(depthBack.size()), getBuildThreadCount(), [&](int, int i0, int i1) {
                for (int i = i0; i < i1; i++)
                {
                    out[i] = (d[i] < minMM || d[i] > maxMM) ? 0 : static_cast<unsigned char>(255.f - (d[i] - minMM) * scale);
                }
            });

            if (settings.usePointCloud)
            {
                const uint64_t buildStart = ofGetElapsedTimeMicros();
                buildPointCloud(back, options, cloudBack);
                buildAverage = ofLerp(buildAverage, (ofGetElapsedTimeMicros() - buildStart) / 1000.f, 0.1f);
                cloudInColourSpace = options.bigPointCloud && back.hasBigDepth;
            }
        }
        if (back.hasIr)
        {
            irBack.allocate(back.ir.getWidth(), back.ir.getHeight(), OF_PIXELS_GRAY);
            const float* ir = back.ir.getData();
            unsigned char* out = irBack.getData();
            for (std::size_t i = 0; i < irBack.size(); i++) out[i] = static_cast<unsigned char>(ir[i]);
        }

        float offsetMs = -1.f;
        if (auto c = fs.get_color_frame())
        {
            if (auto d = fs.get_depth_frame()) offsetMs = static_cast<float>(std::abs(c.get_timestamp() - d.get_timestamp()));
        }

        const uint64_t now = ofGetElapsedTimeMicros();
        if (lastFrameTime > 0) fpsAverage = ofLerp(fpsAverage, 1e6f / std::max<uint64_t>(1, now - lastFrameTime), 0.1f);
        lastFrameTime = now;
        processAverage = ofLerp(processAverage, (now - start) / 1000.f, 0.1f);

        lock();
        stats.fps = fpsAverage;
        stats.processMs = processAverage;
        stats.buildMs = buildAverage;
        stats.connected = true;
        if (stats.lastError == "disconnected")
        {
            stats.lastError.clear();
            trackDevice(serial, profile.get_device());
        }
        stats.droppedFrames = dropped;
        stats.colorDepthOffsetMs = offsetMs;
        swapFrames(back, framesFront);
        depthBack.swap(depthPixelsFront);
        irBack.swap(irPixelsFront);
        swapCloud(cloudBack, cloudFront);
        bCloudInColourSpaceFront = cloudInColourSpace;
        bNewBuffer = true;
        unlock();
    }
}

int ofxLibRealSense2::getBuildThreadCount()
{
    // Share the machine between the open cameras: each camera already has its own capture
    // thread plus librealsense's USB and processing threads, so one camera may use every core
    // for its point cloud but four cameras get a couple each.
    const int hw = std::max(1u, std::thread::hardware_concurrency());
    const int cameras = std::max(1, openCameraCount.load());
    return ofClamp(hw / cameras, 1, 8);
}

void ofxLibRealSense2::buildPointCloud(const Frames& frames, const CloudOptions& o, Cloud& cloud)
{
    cloud.positions.clear();
    cloud.normals.clear();
    cloud.texCoords.clear();
    cloud.colors.clear();
    cloud.indices.clear();

    // The grid the cloud is sampled on: the depth image (depth camera), or depth re-projected
    // into the colour image (colour camera).
    const bool big = o.bigPointCloud && frames.hasBigDepth;
    const ofFloatPixels& depthImage = big ? frames.bigDepth : frames.depth;
    const rs2_intrinsics& intrinsics = big ? frames.colorIntrinsics : frames.depthIntrinsics;
    const int w = static_cast<int>(depthImage.getWidth());
    const int h = static_cast<int>(depthImage.getHeight());
    if (w == 0 || h == 0 || intrinsics.width != w || intrinsics.height != h) return;

    const float* depth = depthImage.getData();
    const int threads = o.threads > 0 ? o.threads : getBuildThreadCount();
    const auto& rays = raysFor(intrinsics, threads);
    const bool flip = frames.mirrored;
    // At colour resolution one depth pixel covers several colour pixels: filter over the same
    // physical neighbourhood.
    const float pixelRatio = (big && frames.depthIntrinsics.fx > 0.f) ? frames.colorIntrinsics.fx / frames.depthIntrinsics.fx : 1.f;
    const int denoiseSpacing = std::max(1, static_cast<int>(std::lround(pixelRatio)));

    // Colour from the colour image: directly at colour resolution, through the depth ->
    // colour map at depth resolution (-1 where the colour camera can't see the point).
    const bool hasColor = frames.hasColor && frames.color.isAllocated()
        && (big || frames.colorMap.size() == static_cast<size_t>(w) * h);
    const int colorW = hasColor ? static_cast<int>(frames.color.getWidth()) : 0;
    const bool useTexCoords = o.texCoords && hasColor;
    const bool useColors = !o.texCoords && hasColor;
    const int channels = hasColor ? static_cast<int>(frames.color.getNumChannels()) : 0;
    const unsigned char* color = hasColor ? frames.color.getData() : nullptr;
    const int* colorMap = (hasColor && !big) ? frames.colorMap.data() : nullptr;

    const float minZ = o.minDistance;
    const float maxZ = o.maxDistance;
    const int step = std::max(1, o.steps);

    auto pointAt = [&](int u, int v, glm::vec3& p) {
        const float z = (o.denoise ? denoisedDepthAt(depth, w, h, u, v, denoiseSpacing) : depth[v * w + u]) * 0.001f;
        if (!(z > minZ && z < maxZ)) return false; // also rejects NaN, inf and holes (0)
        // stored images may be flipped: calibration is in the camera's own columns, and a
        // mirrored image gets a mirrored cloud
        const glm::vec2 r = rays[static_cast<size_t>(v) * w + (flip ? w - 1 - u : u)];
        p = glm::vec3((flip ? -r.x : r.x) * z, r.y * z, z);
        return true;
    };
    // Index of the sample's colour pixel, -1 for none.
    auto colorIndexAt = [&](int u, int v) {
        if (!hasColor) return -1;
        return big ? v * colorW + u : colorMap[v * w + u];
    };
    auto colorOf = [&](int c) {
        const unsigned char* p = color + static_cast<size_t>(c) * channels;
        return ofFloatColor(p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, o.alpha);
    };

    const int gw = (w - 1) / step + 1;
    const int gh = (h - 1) / step + 1;

    if (!o.faces)
    {
        // Points: only valid samples, compacted. With colour, points the colour camera can't
        // see are dropped too. Each band of rows fills its own buffers; they're joined in
        // order afterwards.
        struct Band { std::vector<glm::vec3> p; std::vector<glm::vec2> t; std::vector<ofFloatColor> c; };
        std::vector<Band> bands(threads);
        parallelRows(gh, threads, [&](int band, int gy0, int gy1) {
            Band& b = bands[band];
            for (int gy = gy0; gy < gy1; ++gy)
            {
                const int v = gy * step;
                for (int gx = 0; gx < gw; ++gx)
                {
                    const int u = gx * step;
                    glm::vec3 p;
                    if (!pointAt(u, v, p)) continue;
                    const int c = colorIndexAt(u, v);
                    if (hasColor && c < 0) continue;
                    b.p.push_back(p);
                    if (useTexCoords) b.t.emplace_back(c % colorW, c / colorW);
                    if (useColors) b.c.push_back(colorOf(c));
                }
            }
        });
        size_t total = 0;
        for (auto& b : bands) total += b.p.size();
        cloud.positions.reserve(total);
        if (useTexCoords) cloud.texCoords.reserve(total);
        if (useColors) cloud.colors.reserve(total);
        for (auto& b : bands)
        {
            cloud.positions.insert(cloud.positions.end(), b.p.begin(), b.p.end());
            cloud.texCoords.insert(cloud.texCoords.end(), b.t.begin(), b.t.end());
            cloud.colors.insert(cloud.colors.end(), b.c.begin(), b.c.end());
        }
    }
    else
    {
        // Mesh: one vertex per grid sample (vertex = gy * gw + gx), so the step decimates the
        // vertex buffer too. Invalid samples stay at the origin and are never indexed.
        const size_t count = static_cast<size_t>(gw) * gh;
        cloud.positions.assign(count, glm::vec3(0.f));
        cloud.normals.assign(count, glm::vec3(0.f));
        if (useTexCoords) cloud.texCoords.assign(count, glm::vec2(0.f));
        if (useColors) cloud.colors.assign(count, ofFloatColor(0.f, 0.f));
        std::vector<uint8_t> valid(count, 0);
        auto& pos = cloud.positions;

        parallelRows(gh, threads, [&](int, int gy0, int gy1) {
            for (int gy = gy0; gy < gy1; ++gy)
            {
                const int v = gy * step;
                for (int gx = 0; gx < gw; ++gx)
                {
                    const int u = gx * step;
                    const size_t i = static_cast<size_t>(gy) * gw + gx;
                    if (!pointAt(u, v, pos[i])) continue;
                    const int c = colorIndexAt(u, v);
                    if (hasColor && c < 0) continue;
                    valid[i] = 1;
                    if (useTexCoords) cloud.texCoords[i] = glm::vec2(c % colorW, c / colorW);
                    if (useColors) cloud.colors[i] = colorOf(c);
                }
            }
        });

        // A triangle is kept when all three corners are valid, every edge is shorter than
        // facesMaxLength, and the corners' depths differ by less than facesMaxDepthJump of
        // their distance - this is what stops "skin" being stretched between a person and
        // the wall behind them. Both limits are per depth pixel and grow with the step; a
        // colour-resolution grid step is a fraction of a depth pixel.
        const float gridScale = big ? step / pixelRatio : static_cast<float>(step);
        const float maxEdge = o.facesMaxLength * std::max(1.f, gridScale);
        const float maxEdge2 = maxEdge * maxEdge;
        const float maxJump = o.facesMaxDepthJump * std::max(1.f, gridScale);

        auto edgeOk = [&](size_t a, size_t b) {
            const glm::vec3 d = pos[a] - pos[b];
            return glm::dot(d, d) < maxEdge2 && std::abs(d.z) < maxJump * std::min(pos[a].z, pos[b].z);
        };
        auto triangleOk = [&](size_t a, size_t b, size_t c) {
            return valid[a] && valid[b] && valid[c] && edgeOk(a, b) && edgeOk(b, c) && edgeOk(c, a);
        };
        auto dist2 = [&](size_t a, size_t b) { const glm::vec3 d = pos[a] - pos[b]; return glm::dot(d, d); };

        // Normals from the neighbouring samples on the grid (central differences, falling back
        // to one-sided ones at holes and depth edges), turned to face the camera.
        parallelRows(gh, threads, [&](int, int gy0, int gy1) {
            for (int gy = gy0; gy < gy1; ++gy)
            {
                for (int gx = 0; gx < gw; ++gx)
                {
                    const size_t i = static_cast<size_t>(gy) * gw + gx;
                    if (!valid[i]) continue;
                    auto neighbour = [&](int nx, int ny) -> size_t {
                        if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) return i;
                        const size_t n = static_cast<size_t>(ny) * gw + nx;
                        return (valid[n] && edgeOk(i, n)) ? n : i;
                    };
                    const glm::vec3 dx = pos[neighbour(gx + 1, gy)] - pos[neighbour(gx - 1, gy)];
                    const glm::vec3 dy = pos[neighbour(gx, gy + 1)] - pos[neighbour(gx, gy - 1)];
                    glm::vec3 n = glm::cross(dx, dy);
                    const float len2 = glm::dot(n, n);
                    if (len2 < 1e-20f) continue;
                    n /= std::sqrt(len2);
                    cloud.normals[i] = glm::dot(n, pos[i]) > 0.f ? -n : n;
                }
            }
        });

        // Triangles, one index list per band of rows, joined in order. Every triangle is wound
        // to face the camera.
        std::vector<std::vector<ofIndexType>> bandIndices(threads);
        parallelRows(gh - 1, threads, [&](int band, int gy0, int gy1) {
            auto& out = bandIndices[band];
            out.reserve(static_cast<size_t>(gy1 - gy0) * (gw - 1) * 6);
            auto push = [&](size_t a, size_t b, size_t c) {
                const glm::vec3 n = glm::cross(pos[b] - pos[a], pos[c] - pos[a]);
                if (glm::dot(n, pos[a]) > 0.f) std::swap(b, c);
                out.push_back(static_cast<ofIndexType>(a));
                out.push_back(static_cast<ofIndexType>(b));
                out.push_back(static_cast<ofIndexType>(c));
            };
            for (int gy = gy0; gy < gy1; ++gy)
            {
                for (int gx = 0; gx < gw - 1; ++gx)
                {
                    const size_t tl = static_cast<size_t>(gy) * gw + gx;
                    const size_t tr = tl + 1;
                    const size_t bl = tl + gw;
                    const size_t br = bl + 1;
                    const int validCount = valid[tl] + valid[tr] + valid[bl] + valid[br];
                    if (validCount < 3) continue;

                    if (validCount == 4)
                    {
                        // Split along the shorter diagonal.
                        if (dist2(tl, br) <= dist2(tr, bl))
                        {
                            if (triangleOk(tl, bl, br)) push(tl, bl, br);
                            if (triangleOk(tl, br, tr)) push(tl, br, tr);
                        }
                        else
                        {
                            if (triangleOk(tl, bl, tr)) push(tl, bl, tr);
                            if (triangleOk(tr, bl, br)) push(tr, bl, br);
                        }
                    }
                    else
                    {
                        // One corner missing: the remaining triangle still closes the surface.
                        if (!valid[tl]) { if (triangleOk(tr, bl, br)) push(tr, bl, br); }
                        else if (!valid[tr]) { if (triangleOk(tl, bl, br)) push(tl, bl, br); }
                        else if (!valid[bl]) { if (triangleOk(tl, br, tr)) push(tl, br, tr); }
                        else { if (triangleOk(tl, bl, tr)) push(tl, bl, tr); }
                    }
                }
            }
        });
        size_t total = 0;
        for (auto& b : bandIndices) total += b.size();
        cloud.indices.reserve(total);
        for (auto& b : bandIndices) cloud.indices.insert(cloud.indices.end(), b.begin(), b.end());
    }

    if (o.transform)
    {
        // Inverse transpose keeps normals perpendicular under non-uniform scale.
        const glm::mat4 m = o.transformation;
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(m)));
        const bool hasNormals = !cloud.normals.empty();
        parallelRows(static_cast<int>(cloud.positions.size()), threads, [&](int, int i0, int i1) {
            for (int i = i0; i < i1; ++i)
            {
                cloud.positions[i] = glm::vec3(m * glm::vec4(cloud.positions[i], 1.f));
                if (hasNormals)
                {
                    const glm::vec3 t = normalMatrix * cloud.normals[i];
                    const float len = glm::length(t);
                    if (len > 1e-12f) cloud.normals[i] = t / len;
                }
            }
        });
    }
}

void ofxLibRealSense2::update()
{
    if (ofGetFrameNum() != lastFrameNo)
    {
        bNewFrame = false;
        lastFrameNo = ofGetFrameNum();
    }

    if (!bOpened) return;

    bool got = false;
    lock();
    if (bNewBuffer)
    {
        swapFrames(framesFront, frames);
        depthPixels.swap(depthPixelsFront);
        irPixels.swap(irPixelsFront);
        swapCloud(cloudFront, cloud);
        bCloudInColourSpace = bCloudInColourSpaceFront;
        bNewBuffer = false;
        got = true;
    }
    unlock();

    if (!got) return;

    if (settings.usePointCloud)
    {
        pointCloud.getVertices().swap(cloud.positions);
        pointCloud.getIndices().swap(cloud.indices);
        pointCloud.getNormals().swap(cloud.normals);
        pointCloud.getTexCoords().swap(cloud.texCoords);
        pointCloud.getColors().swap(cloud.colors);
        pointCloud.setMode(pointCloud.hasIndices() ? OF_PRIMITIVE_TRIANGLES : OF_PRIMITIVE_POINTS);
    }

    bNewFrame = true;
}

void ofxLibRealSense2::drawPointCloud(ofPolyRenderMode renderMode) const
{
    auto programmable = std::dynamic_pointer_cast<ofGLProgrammableRenderer>(ofGetCurrentRenderer());
    const bool restoreLinesShaders = programmable && programmable->areLinesShadersEnabled();
    if (restoreLinesShaders) programmable->disableLinesShaders();

    pointCloud.draw(renderMode);

    if (restoreLinesShaders) programmable->enableLinesShaders();
}

void ofxLibRealSense2::close()
{
    if (bOpened)
    {
        waitForThread(true);
        try
        {
            pipeline.stop();
        }
        catch (const rs2::error& e)
        {
            ofLogWarning("ofxLibRealSense2::close") << serial << ": " << e.what();
        }
        pipeline = rs2::pipeline();
        profile = rs2::pipeline_profile();
        if (!isPlayback()) trackDevice(serial, rs2::device());
        bOpened = false;
        openCameraCount--;
    }
    context.reset();
}

bool ofxLibRealSense2::isOpen() const { return bOpened; }
bool ofxLibRealSense2::isFrameNew() const { return bNewFrame; }
std::string ofxLibRealSense2::getSerial() const { return serial; }
std::string ofxLibRealSense2::getName() const { return name; }

ofxLibRealSense2::CaptureStats ofxLibRealSense2::getCaptureStats()
{
    lock();
    const CaptureStats s = stats;
    unlock();
    return s;
}

ofPixels ofxLibRealSense2::getRgbPixels() { return getPixels(); }
const ofPixels& ofxLibRealSense2::getPixels() const { return frames.color; }
const ofPixels& ofxLibRealSense2::getRegisteredPixels() const { return frames.registered; }
const ofFloatPixels& ofxLibRealSense2::getRawDepthPixels() const { return frames.depth; }
const ofPixels& ofxLibRealSense2::getDepthPixels() const { return depthPixels; }
const ofFloatPixels& ofxLibRealSense2::getRawIRPixels() const { return frames.ir; }
const ofPixels& ofxLibRealSense2::getIRPixels() const { return irPixels; }
const ofFloatPixels& ofxLibRealSense2::getUndistortedDepthPixels() const { return frames.depth; }
const ofVboMesh& ofxLibRealSense2::getPointCloud() const { return pointCloud; }

void ofxLibRealSense2::setPointCloudAlpha(int alpha)
{
    pointCloudAlpha = ofClamp(alpha, 0, 255);
}

void ofxLibRealSense2::setPointCloudTransformationMatrix(const ofMatrix4x4& mat)
{
    // ofMatrix4x4's row-vector "v * M" is glm's column-vector "M * v" on the same memory.
    setPointCloudTransformationMatrix(glm::mat4(mat));
}

void ofxLibRealSense2::setPointCloudTransformationMatrix(const glm::mat4& mat)
{
    {
        std::lock_guard<std::mutex> lock(transformMutex);
        pointCloudTransformationMat = mat;
    }
    bTransformPointCloud = true;
}

glm::mat4 ofxLibRealSense2::getPointCloudTransformationMatrix() const
{
    std::lock_guard<std::mutex> lock(transformMutex);
    return pointCloudTransformationMat;
}

float ofxLibRealSense2::getDistanceAt(std::size_t x, std::size_t y) const
{
    return glm::length(getWorldCoordinateAt(x, y));
}

glm::vec3 ofxLibRealSense2::getWorldCoordinateAt(std::size_t x, std::size_t y) const
{
    const ofFloatPixels& depth = frames.depth;
    if (!depth.isAllocated())
    {
        ofLogWarning("ofxLibRealSense2::getWorldCoordinateAt") << "no depth frame yet, returning 0, 0, 0.";
        return glm::vec3(0.f);
    }
    const int w = static_cast<int>(depth.getWidth());
    if (x >= depth.getWidth() || y >= depth.getHeight())
    {
        ofLogWarning("ofxLibRealSense2::getWorldCoordinateAt") << "invalid x, y coordinates.";
        return glm::vec3(0.f);
    }

    const float z = depth[y * w + x] * 0.001f;
    if (!(z > 0.f)) return glm::vec3(0.f);
    const float pixel[2] = { static_cast<float>(frames.mirrored ? w - 1 - static_cast<int>(x) : static_cast<int>(x)), static_cast<float>(y) };
    float p[3];
    rs2_deproject_pixel_to_point(p, &frames.depthIntrinsics, pixel, z);
    return glm::vec3(frames.mirrored ? -p[0] : p[0], p[1], p[2]);
}

rs2_intrinsics ofxLibRealSense2::getDepthIntrinsics() const
{
    std::lock_guard<std::mutex> lock(calibrationMutex);
    return depthIntrinsics;
}

rs2_intrinsics ofxLibRealSense2::getColorIntrinsics() const
{
    std::lock_guard<std::mutex> lock(calibrationMutex);
    return colorIntrinsics;
}

rs2_extrinsics ofxLibRealSense2::getDepthToColorExtrinsics() const
{
    std::lock_guard<std::mutex> lock(calibrationMutex);
    return depthToColor;
}

float ofxLibRealSense2::getDepthScale() const
{
    std::lock_guard<std::mutex> lock(calibrationMutex);
    return depthScale;
}

rs2::device ofxLibRealSense2::getDevice() const
{
    return bOpened ? profile.get_device() : rs2::device();
}

void ofxLibRealSense2::setPaused(bool paused)
{
    if (auto playback = getDevice().as<rs2::playback>())
    {
        paused ? playback.pause() : playback.resume();
    }
}

bool ofxLibRealSense2::isPaused() const
{
    auto playback = getDevice().as<rs2::playback>();
    return playback && playback.current_status() == RS2_PLAYBACK_STATUS_PAUSED;
}

float ofxLibRealSense2::getDuration() const
{
    auto playback = getDevice().as<rs2::playback>();
    return playback ? std::chrono::duration<float>(playback.get_duration()).count() : 0.f;
}

float ofxLibRealSense2::getPosition() const
{
    auto playback = getDevice().as<rs2::playback>();
    return playback ? playback.get_position() * 1e-9f : 0.f;
}

void ofxLibRealSense2::seek(float seconds)
{
    if (auto playback = getDevice().as<rs2::playback>())
    {
        playback.seek(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<float>(std::max(0.f, seconds))));
    }
}

void ofxLibRealSense2::setUsePointCloud(bool v) { settings.usePointCloud = v; }
void ofxLibRealSense2::setUseRegisterImages(bool v) { settings.registerImages = v; }
void ofxLibRealSense2::setIsPointCloudFilled(bool v) { pointCloudHasFaces = v; }
void ofxLibRealSense2::setUseRgb(bool v) { settings.enableRGB = v; }
void ofxLibRealSense2::setUseDepth(bool v) { settings.enableDepth = v; }
void ofxLibRealSense2::setUseIr(bool v) { settings.enableIR = v; }
void ofxLibRealSense2::setUseTexCoords(bool v) { settings.pointCloudTexCoords = v; }
void ofxLibRealSense2::setTransformPointCloud(bool v) { bTransformPointCloud = v; }

bool ofxLibRealSense2::getUsePointCloud() const { return settings.usePointCloud; }
bool ofxLibRealSense2::getUseRegisterImages() const { return settings.registerImages; }
bool ofxLibRealSense2::getIsPointCloudFilled() const { return pointCloudHasFaces; }
bool ofxLibRealSense2::getUseRgb() const { return settings.enableRGB; }
bool ofxLibRealSense2::getUseDepth() const { return settings.enableDepth; }
bool ofxLibRealSense2::getUseIr() const { return settings.enableIR; }
bool ofxLibRealSense2::getUseTexCoords() const { return settings.pointCloudTexCoords; }
bool ofxLibRealSense2::getTransformPointCloud() const { return bTransformPointCloud; }
