//
//  ofxLibRealSense2.cpp
//  example
//
//  Created by shiyamon on 2018/05/25.
//

#include "ofxLibRealSense2.hpp"
#include "ofSystemUtils.h"
#include "ofLog.h"

using namespace::std;

int ofxLibRealSense2::getDeviceCount()
{
    //// query device
    rs2::context ctx;
    return ctx.query_devices().size();
}


void ofxLibRealSense2::setupDevice(int deviceID)
{
    // query device
    rs2::context ctx;
    rs2::device_list deviceList = ctx.query_devices();
    
    if(deviceList.size() <= 0) {
        ofSystemAlertDialog("RealSense device not found!");
        return;
    }
    if (deviceID >= deviceList.size()) {
        ofSystemAlertDialog("Requested device id is invalid");
        return;
    }
    
    _device = deviceList[deviceID];
    string deviceSerial = _device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
    _config.enable_device(deviceSerial);
    cout << "Device name is: " << _device.get_info(RS2_CAMERA_INFO_NAME) << endl;
    
    _curDeviceID = deviceID;
    _setupFinished = true;
    
    setupGUI(deviceSerial);
}


void ofxLibRealSense2::setupColor(int width, int height, int fps)
{
    _colorWidth = width;
    _colorHeight = height;
    _colTex.allocate(_colorWidth, _colorHeight, GL_RGB);
    _config.enable_stream(RS2_STREAM_COLOR, -1, _colorWidth, _colorHeight, RS2_FORMAT_RGB8, fps);
    _colorEnabled = true;
}


void ofxLibRealSense2::setupIR(int width, int height, int fps)
{
    _irWidth = width;
    _irHeight = height;
    _irTex.allocate(_irWidth, _irHeight, GL_LUMINANCE);
    _config.enable_stream(RS2_STREAM_INFRARED, 1, _irWidth, _irHeight, RS2_FORMAT_Y8, fps);
    _irEnabled = true;
}


void ofxLibRealSense2::setupDepth(int width, int height, int fps)
{
    _depthWidth = width;
    _depthHeight = height;
    _depthTex.allocate(_depthWidth, _depthHeight, GL_RGB);
    _rawDepthTex.allocate(_depthWidth, _depthHeight, GL_LUMINANCE16);
    _config.enable_stream(RS2_STREAM_DEPTH, -1, _depthWidth, _depthHeight, RS2_FORMAT_Z16, fps);
    _colorizer.set_option(RS2_OPTION_COLOR_SCHEME, 2);
    _depthEnabled = true;
}


void ofxLibRealSense2::startPipeline(bool useThread)
{
    if(!_setupFinished) return;
    
    _pipeline.start(_config);
    _pipelineStarted=true;
    
    _useThread = useThread;
    if(_useThread)
        startThread();
}



void ofxLibRealSense2::threadedFunction()
{
    while(isThreadRunning()) {
        
        if(lock()) {
            updateFrameData();
            unlock();
        }
    }
}


void ofxLibRealSense2::updateFrameData()
{
    rs2::frameset frameset;
    if(_pipeline.poll_for_frames(&frameset)) {
        if(_colorEnabled) {
            rs2::video_frame colFrame = frameset.get_color_frame();
            _colBuff = (uint8_t*)colFrame.get_data();
            _colorWidth = colFrame.get_width();
            _colorHeight = colFrame.get_height();
            _hasNewColor = true;
        }
        if(_irEnabled) {
            rs2::video_frame irFrame = frameset.get_infrared_frame();
            _irBuff = (uint8_t*)irFrame.get_data();
            _irWidth = irFrame.get_width();
            _irHeight = irFrame.get_height();
            _hasNewIr = true;
        }
        if(_depthEnabled) {
            rs2::depth_frame depthFrame = frameset.get_depth_frame();
            _rawDepthBuff = (uint16_t*)depthFrame.get_data();
            if(_alignDepth) {
                rs2::frameset frames = _pipeline.wait_for_frames();
                rs2::align align(RS2_STREAM_COLOR);
                auto aligned_frames = align.process(frames);
                depthFrame = aligned_frames.get_depth_frame();
            }
            rs2::video_frame normalizedDepthFrame = _colorizer.process(depthFrame);
            _depthBuff = (uint8_t*)normalizedDepthFrame.get_data();

            _depthWidth = depthFrame.get_width();
            _depthHeight = depthFrame.get_height();
            _hasNewDepth = true;
        }
    }
}


void ofxLibRealSense2::update()
{
    if(!_pipelineStarted) return;
    
    rs2::frameset frameset;
    if( !_useThread ) {
        updateFrameData();
    }
    
    _hasNewFrame = _hasNewColor | _hasNewIr | _hasNewDepth;

    if(_depthBuff && _hasNewDepth) {
        _rawDepthTex.loadData(_rawDepthBuff, _depthWidth, _depthHeight, GL_LUMINANCE);
        _depthTex.loadData(_depthBuff, _depthWidth, _depthHeight, GL_RGB);
        _hasNewDepth = false;
    }

    if(_irBuff && _hasNewIr) {
        _irTex.loadData(_irBuff, _irWidth, _irHeight, GL_LUMINANCE);
        _hasNewIr = false;
    }

    if(_colBuff && _hasNewColor) {
        _colTex.loadData(_colBuff, _colorWidth, _colorHeight, GL_RGB);
        _hasNewColor = false;
    }
}


void ofxLibRealSense2::setupGUI(string serialNumber)
{
    rs2::sensor sensor = _device.query_sensors()[0];
    rs2::option_range orExp = sensor.get_option_range(RS2_OPTION_EXPOSURE);
    rs2::option_range orGain = sensor.get_option_range(RS2_OPTION_GAIN);
    rs2::option_range orMinDist = _colorizer.get_option_range(RS2_OPTION_MIN_DISTANCE);
    rs2::option_range orMaxDist = _colorizer.get_option_range(RS2_OPTION_MAX_DISTANCE);

    _D400Params.setup("D400_" + serialNumber);
    _D400Params.add( _autoExposure.setup("Auto exposure", true) );
    _D400Params.add( _enableEmitter.setup("Emitter", true) );
    _D400Params.add( _alignDepth.setup("Align depth to coloer", true) );
    _D400Params.add( _irExposure.setup("IR Exposure", orExp.def, orExp.min, 26000 ));
    _D400Params.add( _depthMin.setup("Min Depth", orMinDist.def, orMinDist.min, orMinDist.max));
    _D400Params.add( _depthMax.setup("Max Depth", orMaxDist.def, orMaxDist.min, orMaxDist.max));
    
    _autoExposure.addListener(this, &ofxLibRealSense2::onD400BoolParamChanged);
    _enableEmitter.addListener(this, &ofxLibRealSense2::onD400BoolParamChanged);
    _irExposure.addListener(this, &ofxLibRealSense2::onD400IntParamChanged);
    _depthMin.addListener(this, &ofxLibRealSense2::onD400ColorizerParamChanged);
    _depthMax.addListener(this, &ofxLibRealSense2::onD400ColorizerParamChanged);
}


void ofxLibRealSense2::onD400BoolParamChanged(bool &value)
{
    if(!_pipelineStarted) return;
    rs2::sensor sensor = _pipeline.get_active_profile().get_device().first<rs2::depth_sensor>();
    if(sensor.supports(RS2_OPTION_ENABLE_AUTO_EXPOSURE))
        sensor.set_option(RS2_OPTION_ENABLE_AUTO_EXPOSURE, _autoExposure?1.0f:0.0f);
    if(sensor.supports(RS2_OPTION_EMITTER_ENABLED))
        sensor.set_option(RS2_OPTION_EMITTER_ENABLED, _enableEmitter?1.0f:0.0f);
}


void ofxLibRealSense2::onD400IntParamChanged(int &value)
{
    if(!_pipelineStarted) return;
    rs2::sensor sensor = _pipeline.get_active_profile().get_device().first<rs2::depth_sensor>();
    if(sensor.supports(RS2_OPTION_EXPOSURE))
        sensor.set_option(RS2_OPTION_EXPOSURE, (float)_irExposure);
}


void ofxLibRealSense2::onD400ColorizerParamChanged(float &value)
{
    if(!_pipelineStarted) return;
    _colorizer.set_option(RS2_OPTION_HISTOGRAM_EQUALIZATION_ENABLED, 0);
    
    if(_colorizer.supports(RS2_OPTION_MIN_DISTANCE))
        _colorizer.set_option(RS2_OPTION_MIN_DISTANCE, _depthMin);
    if(_colorizer.supports(RS2_OPTION_MAX_DISTANCE))
        _colorizer.set_option(RS2_OPTION_MAX_DISTANCE, _depthMax);
}


ofxGuiGroup* ofxLibRealSense2::getGui()
{
    return &_D400Params;
}


void ofxLibRealSense2::exit()
{
    waitForThread();
    stopThread();
//    _pipeline.stop();
}
