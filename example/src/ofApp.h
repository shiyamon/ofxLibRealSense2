#pragma once

#include "ofMain.h"
#include "ofxLibRealSense2.hpp"

class ofApp : public ofBaseApp{

public:
    void setup();
    void update();
    void draw();
    void exit();
    void keyPressed(int key);
    
    ofxLibRealSense2 _realsense2;
    ofxPanel         _gui;
};
