#pragma once

// ofxLibRealSense2 recording and playback: record the camera to a .bag file, then play it back
// through the same API as a live camera (images, point cloud, stats). Recordings from
// RealSense Viewer play too: drop a .bag file on the window.
//
// [r] start / stop recording (bin/data/recording-<date>.bag)   [p] play the last recording
// [l] back to the live camera   [space] pause   [left / right] seek 5 s   [g] gui

#include "ofMain.h"

#include "ofxLibRealSense2.h"
#include "ofxGui.h"

class ofApp : public ofBaseApp
{
public:
	void setup();
	void exit();
	void update();
	void draw();
	void keyPressed(int key);
	void dragEvent(ofDragInfo dragInfo);

private:
	void openLive(const std::string& recordTo = "");
	void openRecording(const std::string& path);

	ofxLibRealSense2 camera;
	ofxLibRealSense2::Settings settings;
	std::string lastRecording;
	bool recording = false;

	ofTexture texColour;
	ofTexture texDepth;
	ofEasyCam cam;

	ofxPanel panel;
	ofParameter<float> position;
	ofParameter<bool> paused;
	ofParameter<bool> realtime;
	bool updatingPosition = false;
	ofEventListeners listeners;
	bool showGui = true;
};
