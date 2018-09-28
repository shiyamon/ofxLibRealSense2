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
    
    vector<ofxLibRealSense2*> _rsList;
    ofxPanel         _gui;
};
