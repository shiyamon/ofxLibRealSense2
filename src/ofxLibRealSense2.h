//
//  ofxLibRealSense2.h
//
//  One Intel RealSense depth camera (D400 / D500 series) through librealsense 2. Frames are
//  grabbed, filtered, registered and turned into a point cloud (or triangulated mesh) on a
//  worker thread; update() hands the latest frame to the app. Live cameras and recorded .bag
//  files work the same way.
//
//  The API follows ofxKinectV2, so apps can switch between the two (or mix them): the same
//  Settings, accessors, parameters, point cloud builder and capture stats. Point clouds use
//  the same conventions as ofxKinectV2 and ofxK4a: metres, camera space with +X right, +Y
//  down, +Z forward (looking out of the sensor), then the optional transformation matrix
//  (camera -> world). The camera is the depth camera, or the colour camera with "Colour-res
//  point cloud" on. Texture coordinates are pixels of the colour image (getPixels()).
//

#pragma once

#include "ofMain.h"

#include <librealsense2/rs.hpp>
#include "ofxLibRealSense2Processing.h"

#include <atomic>
#include <memory>
#include <mutex>

class ofxLibRealSense2: public ofThread
{
public:
    struct DeviceInfo
    {
        std::string serial;
        std::string name;        ///< e.g. "Intel RealSense D415"
        std::string usbType;     ///< e.g. "3.2" - depth needs USB 3 for full resolution
        std::string firmware;
        int deviceId;            ///< index in listDevices(), which is sorted by serial
    };
    /// ofxKinectV2 spelling.
    using KinectDeviceInfo = DeviceInfo;

    /// Hardware sync between cameras (D400 series, sync cable between the cameras' sync
    /// connectors): one MASTER, the others SLAVE, and their frames are exposed together.
    enum class SyncMode
    {
        DEFAULT = 0, ///< free running
        MASTER = 1,
        SLAVE = 2
    };

    struct Settings
    {
        bool enableRGB = true;
        bool enableIR = true;
        bool enableDepth = true;
        /// Colour mapped onto the depth image (getRegisteredPixels()). Needs RGB and depth.
        bool registerImages = true;

        /// Stream sizes and rate, 0 = the camera's default (D415: 1280 x 720 depth and colour,
        /// D435: 848 x 480 depth, 1280 x 720 colour, both at 30 fps). Depth and IR share a size.
        int depthWidth = 0;
        int depthHeight = 0;
        int colorWidth = 0;
        int colorHeight = 0;
        int fps = 0;

        bool usePointCloud = true;
        /// Triangulated mesh instead of points. Can be changed later with the "Point cloud
        /// faces" parameter.
        bool pointCloudHasFaces = false;
        /// Build the cloud at colour resolution (colour camera space) instead of depth
        /// resolution (depth camera space). Can be changed later with "Colour-res point cloud".
        bool bigPointCloud = false;
        /// true: texture coordinates into getPixels() (pixels - use ARB textures)
        /// false: per-vertex colours.
        bool pointCloudTexCoords = true;

        /// true (default): images and clouds as the camera sees them. false: mirrored, like a
        /// webcam preview (and ofxKinectV2's raw libfreenect2 images).
        bool unmirror = true;

        /// Depth range kept in the depth images and the cloud, metres (also the "Min / Max
        /// distance" parameters).
        float minDepth = 0.2f;
        float maxDepth = 6.f;

        /// librealsense's post-processing filters, applied on the capture thread before
        /// registration (also parameters). Decimation 2-8 shrinks depth by that factor.
        int decimation = 1;
        bool spatialFilter = true;   ///< edge-preserving smoothing
        bool temporalFilter = true;  ///< smoothing over time (adds a little lag)
        bool holeFilling = false;

        /// Multi-camera hardware sync (see SyncMode).
        SyncMode syncMode = SyncMode::DEFAULT;

        /// Play a recording (.bag, e.g. from RealSense Viewer or recordFile) instead of a
        /// camera. open() then ignores the serial / device id.
        std::string playbackFile;
        /// Play recordings at their recorded speed (true) or as fast as they're processed.
        bool playbackRealtime = true;
        bool playbackLoop = true;
        /// Record the camera's streams to this .bag file while it's open.
        std::string recordFile;
    };

    ofxLibRealSense2();
    ~ofxLibRealSense2();

    /// Connected cameras, sorted by serial so deviceId is stable between runs.
    static std::vector<DeviceInfo> listDevices();
    static std::size_t getDeviceCount();

    std::vector<DeviceInfo> getDeviceList() const { return listDevices(); }
    std::size_t getNumDevices() const { return getDeviceCount(); }

    bool open(const std::string& serial, const Settings& settings);
    bool open(int deviceId, const Settings& settings);
    bool open(const std::string& serial);
    bool open(int deviceId = 0);
    /// Plays a recording (shortcut for Settings::playbackFile).
    bool openFile(const std::string& path, const Settings& settings);
    bool openFile(const std::string& path);

    /// \brief Hands the latest frame from the capture thread to the app.
    void update();

    void close();

    bool isOpen() const;

    /// \returns true if update() received a new frame this app frame.
    bool isFrameNew() const;

    std::string getSerial() const;
    std::string getName() const;
    const Settings& getSettings() const { return settings; }
    bool isPlayback() const { return !settings.playbackFile.empty(); }

    /// Capture thread timing (running averages): frames per second delivered, time spent
    /// processing each frame after it arrives (filters, registration, flips, depth / IR
    /// images, point cloud), and the point cloud's share of it. With several cameras each one
    /// has its own thread.
    ///
    /// Plus the camera's health: whether it is still connected, the last error, frames lost
    /// (gaps in the frame counters), and how far apart in time the paired colour and depth
    /// frames were.
    struct CaptureStats
    {
        float fps = 0.f;
        float processMs = 0.f;
        float buildMs = 0.f;
        bool connected = true;
        std::string lastError;
        std::string pipeline;          ///< what's streaming, e.g. "D415 USB 3.2 1280x720@30"
        uint64_t droppedFrames = 0;
        float colorDepthOffsetMs = -1; ///< -1: unknown
    };
    CaptureStats getCaptureStats();

    /// librealsense's version, e.g. "2.59.1".
    static std::string getLibraryVersion();

    /// The librealsense context shared by every camera in the app.
    static std::shared_ptr<rs2::context> getContext();

    /// One synchronised frame set. Depth is millimetres, IR is 0-255. Colour and registered
    /// colour are RGBA.
    struct Frames
    {
        ofPixels color;             ///< colour image
        ofPixels registered;        ///< depth size, colour for each depth pixel, alpha 0 where there's none
        ofFloatPixels depth;        ///< depth image, mm (0 = no depth)
        ofFloatPixels ir;           ///< left IR image, depth size
        ofFloatPixels bigDepth;     ///< colour size, depth seen from the colour camera (only when asked for)
        /// For each depth pixel, the index (y * colourWidth + x) of its colour pixel, or -1
        /// when it has none.
        std::vector<int> colorMap;
        bool hasColor = false;
        bool hasDepth = false;
        bool hasIr = false;
        bool hasRegistered = false;
        bool hasBigDepth = false;
        uint64_t frameNumber = 0;
        double timestampMs = 0;     ///< device time of the depth (else colour) frame
        /// Calibration of these images (depth after decimation). Unmirrored as librealsense
        /// gives them - the flip for Settings::unmirror = false is applied when sampling.
        rs2_intrinsics depthIntrinsics{};
        rs2_intrinsics colorIntrinsics{};
        bool mirrored = false;      ///< images are flipped horizontally (unmirror = false)
    };

    /// \returns the colour pixels (RGBA).
    const ofPixels& getPixels() const;
    OF_DEPRECATED_MSG("Use getPixels()", ofPixels getRgbPixels());

    /// \returns colour registered to the depth image (depth size, RGBA, alpha 0 where there's no colour).
    const ofPixels& getRegisteredPixels() const;

    /// \returns the depth in millimetres.
    const ofFloatPixels& getRawDepthPixels() const;

    /// \returns depth mapped to 0-255 over the min / max distance (near = bright, holes black).
    const ofPixels& getDepthPixels() const;

    /// \returns the IR image (0-255 as floats).
    const ofFloatPixels& getRawIRPixels() const;

    /// \returns the IR image, 8 bit.
    const ofPixels& getIRPixels() const;

    /// \returns the depth in millimetres. RealSense depth is already undistorted: same as
    /// getRawDepthPixels(), kept for ofxKinectV2 code.
    const ofFloatPixels& getUndistortedDepthPixels() const;

    /// The whole current frame set: images plus the depth -> colour map (Frames::colorMap),
    /// e.g. to record frames and mesh them later with buildPointCloud().
    const Frames& getFrames() const { return frames; }

    const ofVboMesh& getPointCloud() const;

    /// Draws the point cloud. With texture coordinates, bind the colour texture first. Prefer
    /// this to getPointCloud().draw(): openFrameworks' GL_POINTS shader samples the bound
    /// texture at gl_PointCoord and ignores per-vertex texture coordinates.
    void drawPointCloud(ofPolyRenderMode renderMode = OF_MESH_FILL) const;

    /// Camera -> world transform applied to the point cloud on the capture thread.
    void setPointCloudTransformationMatrix(const ofMatrix4x4& mat);
    void setPointCloudTransformationMatrix(const glm::mat4& mat);
    glm::mat4 getPointCloudTransformationMatrix() const;

    /// \brief Distance from the camera in metres for pixel x, y of the depth image.
    float getDistanceAt(std::size_t x, std::size_t y) const;

    /// \brief Camera space point in metres for pixel x, y of the depth image (not transformed
    /// by the point cloud matrix). 0, 0, 0 where there is no depth.
    glm::vec3 getWorldCoordinateAt(std::size_t x, std::size_t y) const;

    /// Factory calibration of the streams in use.
    rs2_intrinsics getDepthIntrinsics() const;
    rs2_intrinsics getColorIntrinsics() const;
    rs2_extrinsics getDepthToColorExtrinsics() const;
    /// Metres per depth unit (0.001 on most cameras).
    float getDepthScale() const;

    /// The librealsense device, for anything not wrapped here (options, advanced mode...).
    /// Empty while closed.
    rs2::device getDevice() const;

    // Playback (recordings only).
    void setPaused(bool paused);
    bool isPaused() const;
    /// Seconds.
    float getDuration() const;
    float getPosition() const;
    void seek(float seconds);

    ofParameterGroup params;
    ofParameter<float> minDistance;     ///< metres, point cloud and depth image range
    ofParameter<float> maxDistance;     ///< metres
    ofParameter<bool> pointCloudHasFaces;
    ofParameter<bool> bigPointCloud;
    ofParameter<float> facesMaxLength;  ///< metres, longest triangle edge kept
    ofParameter<float> facesMaxDepthJump; ///< fraction of the distance, see buildPointCloud()
    ofParameter<int> steps;
    ofParameter<bool> denoiseDepth;
    ofParameter<int> pointCloudAlpha;

    // librealsense post-processing (see Settings)
    ofParameter<int> decimation;
    ofParameter<bool> spatialFilter;
    ofParameter<bool> temporalFilter;
    ofParameter<bool> holeFilling;

    // Depth camera
    ofParameter<bool> emitter;          ///< IR pattern projector
    ofParameter<float> laserPower;
    ofParameter<bool> depthAutoExposure;
    ofParameter<float> depthExposure;   ///< microseconds

    // Colour camera
    ofParameter<bool> autoExposure;
    ofParameter<float> exposure;        ///< colour exposure, camera units (D400: 100 us)
    ofParameter<float> gain;
    ofParameter<bool> autoWhiteBalance;
    ofParameter<float> whiteBalance;    ///< Kelvin

    void setPointCloudAlpha(int alpha);

    // Stream options (take effect on the next open()), except the point cloud ones.
    void setUsePointCloud(bool usePointCloud);
    void setUseRegisterImages(bool registerImages);
    void setIsPointCloudFilled(bool pointCloudFilled);
    void setUseRgb(bool enableRGB);
    void setUseDepth(bool enableDepth);
    void setUseIr(bool enableIr);
    void setUseTexCoords(bool useTexCoords);
    void setTransformPointCloud(bool transformPointCloud);

    bool getUsePointCloud() const;
    bool getUseRegisterImages() const;
    bool getIsPointCloudFilled() const;
    bool getUseRgb() const;
    bool getUseDepth() const;
    bool getUseIr() const;
    bool getUseTexCoords() const;
    bool getTransformPointCloud() const;
    /// True when the current point cloud was built at colour resolution (colour camera space).
    bool getPointCloudInColourSpace() const { return bCloudInColourSpace; }

    /// Point cloud buffers, built on the capture thread.
    struct Cloud
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> texCoords;
        std::vector<ofFloatColor> colors;
        std::vector<ofIndexType> indices;
    };

    struct CloudOptions
    {
        float minDistance = 0.2f;       // metres
        float maxDistance = 6.f;        // metres
        bool faces = false;
        float facesMaxLength = 0.05f;   // metres
        float facesMaxDepthJump = 0.03f; // fraction of the distance
        int steps = 1;
        bool bigPointCloud = false;
        bool texCoords = true;
        bool denoise = false;
        bool unmirror = true;
        float alpha = 1.f;
        /// Threads used to build the cloud, 0 = getBuildThreadCount().
        int threads = 0;
        bool transform = false;
        glm::mat4 transformation = glm::mat4(1.f);
    };

    /// Threads each camera uses to build its point cloud: the hardware threads shared between
    /// the cameras that are open.
    static int getBuildThreadCount();

    /// Builds a point cloud / mesh from one frame set: from Frames::depth (depth camera) or
    /// Frames::bigDepth (options.bigPointCloud, colour camera), coloured from Frames::color
    /// through Frames::colorMap. Static and thread-safe, so recorded frames can be meshed the
    /// same way as live ones.
    static void buildPointCloud(const Frames& frames, const CloudOptions& options, Cloud& cloud);

protected:
    void threadedFunction() override;
    bool processFrames(const rs2::frameset& frameset, Frames& out);
    CloudOptions getCloudOptions() const;
    void setupOptionParams();
    void setSensorOption(bool colour, rs2_option option, float value);

    Settings settings;
    std::shared_ptr<rs2::context> context;
    rs2::pipeline pipeline;
    rs2::pipeline_profile profile;
    std::string serial;
    std::string name;
    std::atomic<bool> bOpened{ false };
    std::atomic<bool> bTransformPointCloud{ false };
    bool bCloudInColourSpace = false;
    bool bCloudInColourSpaceFront = false;

    // Calibration of the streams in use, depth as streamed (before decimation).
    rs2_intrinsics depthIntrinsics{};
    rs2_intrinsics colorIntrinsics{};
    rs2_extrinsics depthToColor{};
    float depthScale = 0.001f;
    mutable std::mutex calibrationMutex;

    // Capture thread processing blocks.
    rs2::decimation_filter decimationBlock;
    rs2::disparity_transform toDisparity{ true };
    rs2::spatial_filter spatialBlock; // holds the spatial filter's options; it runs multi-threaded in the addon
    rs2::temporal_filter temporalBlock;
    rs2::disparity_transform toDepth{ false };
    rs2::hole_filling_filter holeFillingBlock;
    std::vector<uint16_t> bigDepthRaw;
    ofxLibRealSense2Processing::AlignScratch alignScratch;
    uint64_t lastColorFrame = 0;
    uint64_t lastDepthFrame = 0;
    uint64_t dropped = 0;

    mutable std::mutex transformMutex;
    glm::mat4 pointCloudTransformationMat = glm::mat4(1.f);
    std::mutex optionMutex;
    ofEventListeners optionListeners;

    // Current frame, owned by the app thread.
    Frames frames;
    ofPixels depthPixels;
    ofPixels irPixels;
    ofVboMesh pointCloud;
    Cloud cloud;

    // Handed over from the capture thread under lock().
    Frames framesFront;
    ofPixels depthPixelsFront;
    ofPixels irPixelsFront;
    Cloud cloudFront;
    bool bNewBuffer = false;
    CaptureStats stats;

    bool bNewFrame = false;
    static std::atomic<int> openCameraCount;
    uint64_t lastFrameNo = std::numeric_limits<uint64_t>::max();
};
