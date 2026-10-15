#pragma once

#include "ofMain.h"

#include "ofxLibRealSense2.h"

#include "ofxGui.h"

class ofApp
	: public ofBaseApp
{
public:
	void setup();
	void exit();

	void update();
	void draw();

	void keyPressed(int key);
	void keyReleased(int key);
	void mouseMoved(int x, int y);
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseEntered(int x, int y);
	void mouseExited(int x, int y);
	void windowResized(int w, int h);
	void dragEvent(ofDragInfo dragInfo);
	void gotMessage(ofMessage msg);

private:
	void resetCamera();

	std::shared_ptr<ofxLibRealSense2> camera;

	// Full-resolution colour frame; the point cloud's tex coords are in its pixel space.
	ofTexture texColour;

	ofEasyCam cam;

	ofxPanel panel;
	ofParameter<float> pointSize;
	ofParameter<bool> wireframe;
	bool showGui = true;
};
