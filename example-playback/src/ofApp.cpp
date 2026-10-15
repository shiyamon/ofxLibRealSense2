#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup()
{
	ofSetLogLevel(OF_LOG_NOTICE);
	ofLogToConsole();
	ofEnableArbTex(); // point cloud UVs are colour-image pixels
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofSetBackgroundColor(0);

	settings.enableIR = false;
	settings.usePointCloud = true;
	settings.pointCloudTexCoords = true;

	panel.setup("Playback", "settings.xml", 10, 10);
	panel.add(position.set("Position (s)", 0.f, 0.f, 1.f));
	panel.add(paused.set("Paused", false));
	panel.add(realtime.set("Realtime", true));
	listeners.push(position.newListener([this](float& s) { if (!updatingPosition) camera.seek(s); }));
	listeners.push(paused.newListener([this](bool& p) { camera.setPaused(p); }));
	listeners.push(realtime.newListener([this](bool&) {
		if (camera.isPlayback()) openRecording(camera.getSettings().playbackFile);
	}));
	panel.add(camera.params);

	openLive();
	cam.setNearClip(1.f);
	cam.setFarClip(100000.f);
	cam.setTarget(glm::vec3(0.f, 0.f, -2000.f));
	cam.setDistance(2500.f);
}

//--------------------------------------------------------------
void ofApp::openLive(const std::string& recordTo)
{
	ofxLibRealSense2::Settings s = settings;
	s.recordFile = recordTo;
	recording = camera.open(0, s) && !recordTo.empty();
	if (recording)
	{
		lastRecording = recordTo;
		ofLogNotice("ofApp") << "recording to " << recordTo;
	}
}

//--------------------------------------------------------------
void ofApp::openRecording(const std::string& path)
{
	ofxLibRealSense2::Settings s = settings;
	s.playbackRealtime = realtime;
	s.playbackLoop = true;
	recording = false;
	if (camera.openFile(path, s))
	{
		position.setMax(std::max(0.1f, camera.getDuration()));
		paused.setWithoutEventNotifications(false);
		ofLogNotice("ofApp") << "playing " << path << " (" << camera.getDuration() << " s, " << camera.getName() << ")";
	}
}

//--------------------------------------------------------------
void ofApp::exit()
{
	camera.close();
}

//--------------------------------------------------------------
void ofApp::update()
{
	camera.update();
	if (camera.isFrameNew())
	{
		auto load = [](ofTexture& tex, const ofPixels& pix) {
			if (!pix.isAllocated()) return;
			if (tex.getWidth() != pix.getWidth() || tex.getHeight() != pix.getHeight()) tex.allocate(pix);
			tex.loadData(pix);
		};
		load(texColour, camera.getPixels());
		load(texDepth, camera.getDepthPixels());
	}
	if (camera.isPlayback())
	{
		updatingPosition = true;
		position = camera.getPosition();
		updatingPosition = false;
	}
}

//--------------------------------------------------------------
void ofApp::draw()
{
	// Colour and depth thumbnails on the right, the point cloud behind everything.
	if (texColour.isAllocated())
	{
		ofEnableDepthTest();
		cam.begin();
		ofPushMatrix();
		ofScale(1000.f, -1000.f, -1000.f); // metres -> millimetres, camera space -> OF
		texColour.bind();
		camera.drawPointCloud();
		texColour.unbind();
		ofPopMatrix();
		cam.end();
		ofDisableDepthTest();

		const float w = 320.f;
		texColour.draw(ofGetWidth() - w - 10, 10, w, w * texColour.getHeight() / texColour.getWidth());
		if (texDepth.isAllocated())
		{
			const float y = 20 + w * texColour.getHeight() / texColour.getWidth();
			texDepth.draw(ofGetWidth() - w - 10, y, w, w * texDepth.getHeight() / texDepth.getWidth());
		}
	}

	std::string status;
	if (!camera.isOpen()) status = "No camera or recording. Plug in a RealSense camera, or drop a .bag file on the window.";
	else
	{
		const auto stats = camera.getCaptureStats();
		status = (camera.isPlayback() ? "PLAYING " + ofFilePath::getFileName(camera.getSettings().playbackFile) : recording ? "RECORDING " + ofFilePath::getFileName(lastRecording) : "LIVE")
			+ "  " + stats.pipeline + "  " + ofToString(stats.fps, 1) + " fps, " + ofToString(stats.processMs, 1) + " ms per frame";
		if (camera.isPlayback()) status += "  " + ofToString(camera.getPosition(), 1) + " / " + ofToString(camera.getDuration(), 1) + " s";
	}
	ofDrawBitmapStringHighlight(status, 10, ofGetHeight() - 30, recording ? ofColor(160, 0, 0) : ofColor::black);
	ofDrawBitmapStringHighlight("[r] record  [p] play last recording  [l] live  [space] pause  [left/right] seek  [g] gui  (or drop a .bag file)", 10, ofGetHeight() - 10);
	if (showGui) panel.draw();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
	switch (key)
	{
	case 'r':
		if (recording) openLive(); // reopening without a record file finishes the .bag
		else openLive(ofToDataPath("recording-" + ofGetTimestampString("%Y-%m-%d-%H-%M-%S") + ".bag", true));
		break;
	case 'p':
		if (!lastRecording.empty())
		{
			camera.close(); // finish writing before reading
			openRecording(lastRecording);
		}
		break;
	case 'l':
		openLive();
		break;
	case ' ':
		paused = !paused;
		break;
	case OF_KEY_LEFT:
		camera.seek(std::max(0.f, camera.getPosition() - 5.f));
		break;
	case OF_KEY_RIGHT:
		camera.seek(camera.getPosition() + 5.f);
		break;
	case 'g':
		showGui = !showGui;
		break;
	}
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo)
{
	for (const auto& file : dragInfo.files)
	{
		if (ofToLower(ofFilePath::getFileExt(file)) == "bag")
		{
			openRecording(file);
			return;
		}
	}
}
