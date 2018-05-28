//
//  ofxLibRealSense2.hpp
//  example
//
//  Created by shiyamon on 2018/05/25.
//

#pragma once
#include "rs.hpp"
#include "ofThread.h"
#include "ofTexture.h"
#include "ofxGui.h"

class ofxLibRealSense2 : public ofThread
{
    
public:
    void setupDevice(int deviceID);
    void setupColor(int width, int height, int fps=60);
    void setupIR(int width, int height, int fps=60);
    void setupDepth(int width, int height, int fps=60);
    void startPipeline(bool useThread);
    void update();
    void exit();
    
    ofTexture*  getColorTex()   { return &_colTex; }
    ofTexture*  getIrTex()      { return &_irTex; }
    ofTexture*  getDepthTex()   { return &_depthTex; }
    
    int getColorWidth() { return _colorWidth; }
    int getColorHeight(){ return _colorHeight; }
    int getIrWidth()    { return _irWidth; }
    int getIrHeight()   { return _irHeight; }
    int getDepthWidth() { return _depthWidth; }
    int getDepthHeight(){ return _depthHeight; }
    bool isFrameNew()   { return _hasNewFrame; }
    
    ofxGuiGroup *getGui();
    
    ofxLibRealSense2() : _setupFinished(false), _colorEnabled(false), _irEnabled(false), _depthEnabled(false), _pipelineStarted(false), _useThread(false) {}
    
private:
    rs2::device_list    _deviceList;
    int             _curDeviceID;
    
    rs2::config     _config;
    rs2::pipeline   _pipeline;
    bool            _useThread;
    bool            _setupFinished;
    bool            _pipelineStarted;
    bool            _colorEnabled, _irEnabled, _depthEnabled;
    int             _colorWidth, _irWidth, _depthWidth;
    int             _colorHeight, _irHeight, _depthHeight;
    
    uint8_t         *_colBuff, *_irBuff;
    uint16_t        *_depthBuff;
    ofTexture       _colTex, _irTex, _depthTex;
    bool            _hasNewColor, _hasNewIr, _hasNewDepth, _hasNewFrame;
    
    ofxGuiGroup     _D400Params;
    ofxToggle       _autoExposure;
    ofxToggle       _enableEmitter;
    ofxIntSlider    _irExposure;
    
    void threadedFunction();
    void updateFrameData();
    void setupGUI();
    void onD400BoolParamChanged(bool &value);
    void onD400IntParamChanged(int &value);
};
