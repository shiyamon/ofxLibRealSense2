#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
    
    ofSetVerticalSync(false);
    _realsense2.setupDevice(0);
    _realsense2.setupColor(640, 360, 30);
    _realsense2.setupIR(640, 360, 30);
    _realsense2.startPipeline(true);
    
    _gui.setup("appSettings.xml");
    _gui.add(_realsense2.getGui());
}

//--------------------------------------------------------------
void ofApp::update(){
    _realsense2.update();
}

//--------------------------------------------------------------
void ofApp::draw(){
    ofBackground(0);
    _realsense2.getColorTex()->draw(0, 0);
    _realsense2.getIrTex()->draw(0, 0);
    
    ofDrawBitmapString(ofToString(ofGetFrameRate()), 10, 10);
    
    _gui.draw();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

void ofApp::exit()
{
    _realsense2.exit();
}
