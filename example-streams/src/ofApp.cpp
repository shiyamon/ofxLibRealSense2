#include "ofApp.h"

namespace
{
	// Colour (and colour registered to depth) arrive as RGBA.
	void loadRgba(ofTexture& tex, const ofPixels& pix)
	{
		if (!pix.isAllocated()) return;
		if (!tex.isAllocated() || tex.getWidth() != pix.getWidth() || tex.getHeight() != pix.getHeight())
		{
			// The pixels carry their format, so openFrameworks uploads them in the right
			// channel order.
			tex.allocate(pix);
			tex.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
		}
		tex.loadData(pix);
	}

	// Single-channel 8-bit images shown as grey.
	void loadGrey(ofTexture& tex, const ofPixels& pix)
	{
		if (!pix.isAllocated()) return;
		if (!tex.isAllocated() || tex.getWidth() != pix.getWidth() || tex.getHeight() != pix.getHeight())
		{
			tex.allocate(pix);
			tex.setRGToRGBASwizzles(true);
		}
		tex.loadData(pix);
	}
}

//--------------------------------------------------------------
void ofApp::setup()
{
	ofSetLogLevel(OF_LOG_NOTICE);
	ofLogToConsole();
	// Point cloud UVs are colour-image pixel coordinates, so rectangle textures are needed.
	ofEnableArbTex();
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofSetBackgroundColor(0);

	panel.setup("Streams", "settings.xml", 10, 70);
	for (int v = 0; v < int(ViewKind::Count); v++)
	{
		panel.add(viewEnabled[v].set(ofToString(v + 1) + " " + viewName(ViewKind(v)), true));
	}
	panel.add(pointSize.set("Point size", 2.f, 1.f, 10.f));

	const auto devices = ofxLibRealSense2::listDevices();
	ofLogNotice(__FUNCTION__) << "Found " << devices.size() << " RealSense cameras.";

	ofxLibRealSense2::Settings settings;
	settings.enableRGB = true;
	settings.enableIR = true;
	settings.enableDepth = true;
	settings.registerImages = true;
	settings.usePointCloud = true;
	settings.pointCloudHasFaces = false;

	for (const auto& device : devices)
	{
		auto camera = std::make_shared<ofxLibRealSense2>();
		if (camera->open(device.serial, settings))
		{
			camera->params.setName("RealSense " + camera->getSerial());
			panel.add(camera->params);
			cameras.push_back(camera);
		}
	}
	textures.resize(cameras.size());

	// Loaded after the devices' params are added so saved values are applied to them.
	panel.loadFromFile("settings.xml");
	resetCamera();
}

//--------------------------------------------------------------
void ofApp::resetCamera()
{
	// The cloud is in metres and drawn scaled to millimetres (see drawPointCloudView), so
	// frame it from 2.5m back with clip planes that don't cull it when flying close.
	cam.setNearClip(1.f);
	cam.setFarClip(100000.f);
	cam.setTarget(glm::vec3(0.f, 0.f, -2000.f));
	cam.setDistance(2500.f);
}

//--------------------------------------------------------------
void ofApp::exit()
{
	for (auto& camera : cameras)
	{
		camera->close();
	}
}

//--------------------------------------------------------------
void ofApp::update()
{
	for (std::size_t d = 0; d < cameras.size(); d++)
	{
		auto& camera = cameras[d];
		camera->update();
		if (camera->isFrameNew())
		{
			updateTextures(d);
		}
	}
}

//--------------------------------------------------------------
void ofApp::updateTextures(std::size_t d)
{
	auto& camera = cameras[d];
	auto& tex = textures[d];

	if (camera->getUseRgb()) loadRgba(tex.colour, camera->getPixels());
	if (camera->getUseDepth()) loadGrey(tex.depth, camera->getDepthPixels());
	if (camera->getUseIr()) loadGrey(tex.ir, camera->getIRPixels());
	if (camera->getUseRegisterImages()) loadRgba(tex.colourInDepth, camera->getRegisteredPixels());
}

//--------------------------------------------------------------
bool ofApp::isViewAvailable(ViewKind kind) const
{
	if (cameras.empty()) return false;
	auto& camera = cameras[currentCamera];
	switch (kind)
	{
	case ViewKind::Colour: return camera->getUseRgb();
	case ViewKind::Depth: return camera->getUseDepth();
	case ViewKind::Ir: return camera->getUseIr();
	case ViewKind::ColourInDepth: return camera->getUseRegisterImages();
	case ViewKind::PointCloud: return camera->getUsePointCloud();
	default: return true;
	}
}

//--------------------------------------------------------------
std::vector<ofApp::ViewKind> ofApp::visibleViews() const
{
	std::vector<ViewKind> views;
	if (maximisedView >= 0 && isViewAvailable(ViewKind(maximisedView)))
	{
		views.push_back(ViewKind(maximisedView));
		return views;
	}
	for (int v = 0; v < int(ViewKind::Count); v++)
	{
		if (viewEnabled[v] && isViewAvailable(ViewKind(v))) views.push_back(ViewKind(v));
	}
	return views;
}

//--------------------------------------------------------------
void ofApp::layoutViews()
{
	// Pick the grid (columns x rows) whose cells are closest to the 16:9-ish shape of the
	// images, so every view is as large as it can be for the current window.
	const auto views = visibleViews();
	viewRects.clear();
	if (views.empty()) return;

	const float w = ofGetWidth(), h = ofGetHeight();
	const float targetAspect = 16.f / 10.f;
	const int n = int(views.size());
	int bestCols = 1;
	float bestScore = -1.f;
	for (int cols = 1; cols <= n; cols++)
	{
		const int rows = (n + cols - 1) / cols;
		const float cellW = w / cols, cellH = h / rows;
		// Area of a targetAspect image fitted into the cell.
		const float fitW = std::min(cellW, cellH * targetAspect);
		const float score = fitW * (fitW / targetAspect);
		if (score > bestScore) { bestScore = score; bestCols = cols; }
	}
	const int cols = bestCols;
	const int rows = (n + cols - 1) / cols;
	const float cellW = w / cols, cellH = h / rows;
	for (int i = 0; i < n; i++)
	{
		viewRects.emplace_back(views[i], ofRectangle((i % cols) * cellW, (i / cols) * cellH, cellW, cellH));
	}
}

//--------------------------------------------------------------
void ofApp::draw()
{
	layoutViews();
	for (auto& vr : viewRects)
	{
		drawView(vr.first, vr.second);
	}

	std::string status;
	if (cameras.empty())
	{
		status = "No RealSense cameras found.";
	}
	else
	{
		auto& camera = cameras[currentCamera];
		const auto stats = camera->getCaptureStats();
		status = "Device " + ofToString(currentCamera + 1) + "/" + ofToString(cameras.size()) + " (" + camera->getSerial() + ", " + stats.pipeline +
			", librealsense " + ofxLibRealSense2::getLibraryVersion() + "): " + ofToString(stats.fps, 1) + " fps, " +
			ofToString(stats.processMs, 1) + " ms per frame (cloud " + ofToString(stats.buildMs, 1) + " ms)";
		if (stats.colorDepthOffsetMs >= 0) status += ", colour-depth " + ofToString(stats.colorDepthOffsetMs, 1) + " ms";
		status += ", dropped " + ofToString(stats.droppedFrames);
		if (!stats.connected)
		{
			ofDrawBitmapStringHighlight("DISCONNECTED: " + stats.lastError, 10, ofGetHeight() - 50, ofColor(160, 0, 0), ofColor::white);
		}
	}
	status += "   app " + ofToString(ofGetFrameRate(), 1) + " fps";
	ofDrawBitmapStringHighlight(status, 10, ofGetHeight() - 30);
	ofDrawBitmapStringHighlight("double-click / 1-5: maximise view  0: grid  space: next device  r: reset 3D view  g: gui  f: fullscreen  s: screenshot", 10, ofGetHeight() - 10);

	if (showGui) panel.draw();
}

//--------------------------------------------------------------
void ofApp::drawView(ViewKind kind, const ofRectangle& area)
{
	auto& camera = cameras[currentCamera];
	auto& tex = textures[currentCamera];
	std::string info;

	switch (kind)
	{
	case ViewKind::Colour:
		if (tex.colour.isAllocated())
		{
			drawTexture(tex.colour, area);
			info = ofToString(tex.colour.getWidth()) + "x" + ofToString(tex.colour.getHeight());
		}
		break;
	case ViewKind::Depth:
		if (tex.depth.isAllocated())
		{
			drawTexture(tex.depth, area);
			info = ofToString(tex.depth.getWidth()) + "x" + ofToString(tex.depth.getHeight());
		}
		break;
	case ViewKind::Ir:
		if (tex.ir.isAllocated())
		{
			drawTexture(tex.ir, area);
			info = ofToString(tex.ir.getWidth()) + "x" + ofToString(tex.ir.getHeight());
		}
		break;
	case ViewKind::ColourInDepth:
		if (tex.colourInDepth.isAllocated())
		{
			drawTexture(tex.colourInDepth, area);
			info = ofToString(tex.colourInDepth.getWidth()) + "x" + ofToString(tex.colourInDepth.getHeight());
		}
		break;
	case ViewKind::PointCloud:
		drawPointCloudView(area);
		info = ofToString(camera->getPointCloud().getNumVertices()) + " points" + (camera->getPointCloud().hasIndices() ? ", mesh" : "");
		break;
	default:
		break;
	}

	ofNoFill();
	ofSetColor(70);
	ofDrawRectangle(area);
	ofFill();
	ofSetColor(255);
	const std::string title = ofToString(int(kind) + 1) + " " + viewName(kind) + (info.empty() ? "" : "  (" + info + ")");
	ofDrawBitmapStringHighlight(title, area.x + 8, area.y + 18);
}

//--------------------------------------------------------------
void ofApp::drawTexture(const ofTexture& tex, const ofRectangle& area, ofRectangle* outImageRect)
{
	// Fit the image inside its area, keeping its aspect ratio.
	ofRectangle r(0, 0, tex.getWidth(), tex.getHeight());
	r.scaleTo(area);
	ofSetColor(255);
	ofEnableAlphaBlending();
	tex.draw(r);
	if (outImageRect) *outImageRect = r;
}

//--------------------------------------------------------------
void ofApp::drawPointCloudView(const ofRectangle& area)
{
	auto& camera = cameras[currentCamera];
	auto& tex = textures[currentCamera];

	// Only drag the 3D view when the mouse is inside it.
	cam.setControlArea(area);

	ofEnableDepthTest();
	cam.begin(area);
	{
		ofDrawAxis(200.f);
		ofPushMatrix();
		// Metres -> millimetres, and flip Y/Z from the camera's space into OF's.
		ofScale(1000.f, -1000.f, -1000.f);
		glPointSize(pointSize);
		ofSetColor(255);
		if (tex.colour.isAllocated())
		{
			// drawPointCloud, not getPointCloud().draw(): the default points shader ignores UVs.
			tex.colour.bind();
			camera->drawPointCloud();
			tex.colour.unbind();
		}
		ofPopMatrix();
	}
	cam.end();
	ofDisableDepthTest();
}

//--------------------------------------------------------------
std::string ofApp::viewName(ViewKind kind)
{
	switch (kind)
	{
	case ViewKind::Colour: return "Colour";
	case ViewKind::Depth: return "Depth";
	case ViewKind::Ir: return "IR";
	case ViewKind::ColourInDepth: return "Colour in depth";
	case ViewKind::PointCloud: return "Point cloud";
	default: return "";
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
	if (key >= '1' && key < '1' + int(ViewKind::Count))
	{
		const int v = key - '1';
		maximisedView = (maximisedView == v) ? -1 : v;
		return;
	}

	switch (key)
	{
	case '0':
		maximisedView = -1;
		break;
	case ' ':
		if (!cameras.empty()) currentCamera = (currentCamera + 1) % cameras.size();
		break;
	case 'r':
		resetCamera();
		break;
	case 'g':
		showGui = !showGui;
		break;
	case 'f':
		ofToggleFullscreen();
		break;
	case 's':
		ofSaveScreen("screenshot_" + ofGetTimestampString() + ".png");
		break;
	}
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button)
{
	if (button != OF_MOUSE_BUTTON_LEFT) return;
	if (showGui && panel.getShape().inside(x, y)) return;

	// Double-click toggles maximising the view under the mouse.
	const float now = ofGetElapsedTimef();
	const bool doubleClick = (now - lastClickTime) < 0.3f && glm::distance(lastClickPos, glm::vec2(x, y)) < 6.f;
	lastClickTime = now;
	lastClickPos = glm::vec2(x, y);
	if (!doubleClick) return;

	for (auto& vr : viewRects)
	{
		if (vr.second.inside(x, y))
		{
			maximisedView = (maximisedView == int(vr.first)) ? -1 : int(vr.first);
			break;
		}
	}
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h)
{
	layoutViews();
}
