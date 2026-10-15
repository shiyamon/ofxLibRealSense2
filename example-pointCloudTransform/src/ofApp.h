#pragma once

// ofxLibRealSense2 point cloud transform example.
//
// The point cloud is transformed by the addon itself (setPointCloudTransformationMatrix), so the positions/normals it hands back are already in
// the new space - what you'd use to align several cameras or export a cloud. The matrix comes
// from a GUI: translate, rotate and scale on any axis, around a movable pivot.
//
// The GUI works in the same axes you see on screen (metres, +Y up, +Z towards the viewer);
// the example converts that into the camera's space (+Y down, +Z forward) for the addon.

#include "ofMain.h"

#include "ofxLibRealSense2.h"
#include "ofxGui.h"

#include "transformManipulator.h"

class ofApp
	: public ofBaseApp
{
public:
	void setup();
	void exit();

	void update();
	void draw();

	void keyPressed(int key);

private:
	void resetCamera();
	void pushTransformToDevice();

	std::shared_ptr<ofxLibRealSense2> camera;
	ofTexture texColour;

	transformManipulator manipulator;

	ofEasyCam cam;

	ofxPanel transformPanel;
	ofxPanel devicePanel;
	ofParameter<float> pointSize;
	ofParameter<bool> wireframe;
	ofParameter<bool> showSensorAxes;
	ofParameter<bool> showTransformGizmo;
	ofParameter<bool> showFloorGrid;
	ofParameter<bool> showMatrix;
	bool showGui = true;
};
