#pragma once

// ofxLibRealSense2 "everything at once" example: every stream the addon produces, side by side.
//
// Views: colour, depth, IR, colour registered to depth and the 3D point cloud. They tile to
// fit the window; double-click a view (or press 1-5) to make it fill the window, and again
// (or 0) to go back to the grid. Space cycles between connected devices.

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
	void mousePressed(int x, int y, int button);
	void windowResized(int w, int h);

private:
	enum class ViewKind { Colour, Depth, Ir, ColourInDepth, PointCloud, Count };

	// Per-device textures, refreshed whenever the device has a new frame.
	struct DeviceTextures
	{
		ofTexture colour, depth, ir, colourInDepth;
	};

	void updateTextures(std::size_t d);
	bool isViewAvailable(ViewKind kind) const;
	std::vector<ViewKind> visibleViews() const;
	void layoutViews();
	void drawView(ViewKind kind, const ofRectangle& area);
	void drawTexture(const ofTexture& tex, const ofRectangle& area, ofRectangle* outImageRect = nullptr);
	void drawPointCloudView(const ofRectangle& area);
	void resetCamera();
	static std::string viewName(ViewKind kind);

	std::vector<std::shared_ptr<ofxLibRealSense2>> cameras;
	std::vector<DeviceTextures> textures;
	std::size_t currentCamera = 0;

	// The views on screen right now and where they are, for hit testing.
	std::vector<std::pair<ViewKind, ofRectangle>> viewRects;
	int maximisedView = -1; // index into ViewKind, or -1 for the grid
	float lastClickTime = 0.f;
	glm::vec2 lastClickPos;

	ofEasyCam cam;

	ofxPanel panel;
	ofParameter<bool> viewEnabled[int(ViewKind::Count)];
	ofParameter<float> pointSize;
	bool showGui = true;
};
