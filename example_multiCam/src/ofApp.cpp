#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
    
    ofSetVerticalSync(false);
    
    _gui.setup("appSettings.xml");
    
    int deviceCnt = ofxLibRealSense2::getDeviceCount();
    for(int i=0; i<deviceCnt; ++i) {
        _rsList.push_back(new ofxLibRealSense2());
        _rsList.back()->setupDevice(i);
        _rsList.back()->setupColor(640, 360, 30);
        _rsList.back()->startPipeline(true);
        _gui.add(_rsList.back()->getGui());
    }
}

//--------------------------------------------------------------
void ofApp::update(){
    for (int i=0; i<_rsList.size(); ++i) {
        _rsList[i]->update();
    }
}

//--------------------------------------------------------------
void ofApp::draw(){
    
    ofBackground(0);
    for (int i=0; i<_rsList.size(); ++i) {
        if(_rsList[i]->colorEnabled())
            _rsList[i]->getColorTex()->draw(640*i, 0);
    }
    
    ofDrawBitmapString(ofToString(ofGetFrameRate()), 10, 10);
    
    _gui.draw();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
    
}

void ofApp::exit()
{
    for(int i=0; i<_rsList.size(); ++i) {
        _rsList[i]->exit();
        delete _rsList[i];
    }
}
