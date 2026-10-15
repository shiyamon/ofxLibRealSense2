#include "ofApp.h"

namespace
{
	// Display space (what the GUI and ofEasyCam use: +Y up, +Z towards the viewer) differs
	// from the camera's space (+Y down, +Z forward) by flipping Y and Z.
	const glm::mat4 kFlipYZ = glm::scale(glm::mat4(1.f), glm::vec3(1.f, -1.f, -1.f));
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
	ofSetBackgroundColor(20);

	// Pivot 2m in front of the sensor, roughly where a subject stands, so rotations spin
	// the scene in place.
	manipulator.setup("Point cloud transform", glm::vec3(0.f, 0.f, -2.f));

	transformPanel.setup("Transform", "transform.xml", 10, 90);
	transformPanel.add(manipulator.params);
	ofParameterGroup display;
	display.setName("Display");
	display.add(pointSize.set("Point size", 2.f, 1.f, 10.f));
	display.add(wireframe.set("Wireframe (faces on)", false));
	display.add(showSensorAxes.set("Sensor axes", true));
	display.add(showTransformGizmo.set("Transform gizmo + pivot", true));
	display.add(showFloorGrid.set("Floor grid", true));
	display.add(showMatrix.set("Show matrix", true));
	transformPanel.add(display);

	camera = std::make_shared<ofxLibRealSense2>();
	ofxLibRealSense2::Settings settings;
	settings.enableRGB = true;
	settings.enableIR = false;
	settings.enableDepth = true;
	settings.registerImages = true;
	settings.usePointCloud = true;
	settings.pointCloudHasFaces = false;
	settings.pointCloudTexCoords = true;

	if (camera->open(0, settings))
	{
		devicePanel.setup("Device", "device.xml", ofGetWidth() - 230, 90);
		camera->params.setName("RealSense " + camera->getSerial());
		devicePanel.add(camera->params);
		devicePanel.loadFromFile("device.xml");
	}

	transformPanel.loadFromFile("transform.xml");
	pushTransformToDevice();
	resetCamera();
}

//--------------------------------------------------------------
void ofApp::pushTransformToDevice()
{
	// The addon transforms positions in camera space, so wrap the display-space matrix:
	// cameraMatrix = F * M * F  (F flips Y/Z and is its own inverse).
	const glm::mat4 cameraMatrix = kFlipYZ * manipulator.getMatrix() * kFlipYZ;
	if (camera) camera->setPointCloudTransformationMatrix(ofMatrix4x4(cameraMatrix));
}

//--------------------------------------------------------------
void ofApp::resetCamera()
{
	// The cloud is in metres and drawn scaled to millimetres; frame it from a few metres back
	// with clip planes that don't cull it when flying close.
	cam.setNearClip(1.f);
	cam.setFarClip(100000.f);
	cam.setTarget(glm::vec3(0.f, 0.f, -2000.f));
	cam.setDistance(3500.f);
}

//--------------------------------------------------------------
void ofApp::exit()
{
	if (camera) camera->close();
}

//--------------------------------------------------------------
void ofApp::update()
{
	if (manipulator.hasChanged())
	{
		pushTransformToDevice();
	}

	if (!camera || !camera->isOpen()) return;
	camera->update();
	if (camera->isFrameNew() && camera->getPixels().isAllocated())
	{
		if (!texColour.isAllocated())
		{
			// Tagged OF_PIXELS_RGBA, so openFrameworks uploads it in the right channel order.
			texColour.allocate(camera->getPixels());
		}
		texColour.loadData(camera->getPixels());
	}
}

//--------------------------------------------------------------
void ofApp::draw()
{
	// Keep the device panel on the right edge when the window resizes.
	devicePanel.setPosition(ofGetWidth() - devicePanel.getWidth() - 10, 90);

	ofEnableDepthTest();
	cam.begin();
	{
		if (showFloorGrid)
		{
			// 1m squares, 1.5m below the sensor.
			ofPushMatrix();
			ofTranslate(0, -1500, 0);
			ofRotateZDeg(90);
			ofSetColor(60);
			ofDrawGridPlane(1000.f, 6, false);
			ofPopMatrix();
		}

		if (showSensorAxes)
		{
			// The sensor sits at the origin, looking down -Z.
			ofDrawAxis(300.f);
			ofSetColor(200);
			ofDrawBitmapString("sensor", 10, 10, 0);
		}

		if (camera && camera->isOpen() && texColour.isAllocated())
		{
			// The cloud comes back from the addon already transformed; only convert metres ->
			// millimetres and camera space -> display space here.
			ofPushMatrix();
			ofScale(1000.f, -1000.f, -1000.f);
			glPointSize(pointSize);
			ofSetColor(255);
			texColour.bind();
			const bool asWireframe = wireframe && camera->getPointCloud().hasIndices();
			camera->drawPointCloud(asWireframe ? OF_MESH_WIREFRAME : OF_MESH_FILL);
			texColour.unbind();
			ofPopMatrix();
		}

		if (showTransformGizmo)
		{
			ofPushMatrix();
			ofScale(1000.f);
			// Where the pivot is before the transform, and where it ends up after it.
			ofSetColor(255, 200, 0);
			ofDrawSphere(manipulator.getPivot(), 0.03f);
			ofMultMatrix(manipulator.getMatrix());
			ofPushMatrix();
			ofTranslate(manipulator.getPivot());
			ofDrawAxis(0.4f);
			ofPopMatrix();
			ofPopMatrix();
		}
	}
	cam.end();
	ofDisableDepthTest();

	std::string info = ofToString(ofGetFrameRate(), 1) + " fps   [r] reset view  [t] reset transform  [g] gui  [f] fullscreen  [s] screenshot";
	if (!camera || !camera->isOpen()) info += "\nNo RealSense camera streaming.";
	ofDrawBitmapStringHighlight(info, 10, 20);

	if (showMatrix)
	{
		// The display-space matrix, row by row (glm stores columns).
		const glm::mat4 m = manipulator.getMatrix();
		std::string text = "Transform matrix (display space, metres)\n";
		for (int row = 0; row < 4; row++)
		{
			for (int col = 0; col < 4; col++)
			{
				text += ofToString(m[col][row], 3, 8, ' ') + " ";
			}
			text += "\n";
		}
		ofDrawBitmapStringHighlight(text, 10, ofGetHeight() - 80);
	}

	if (showGui)
	{
		transformPanel.draw();
		devicePanel.draw();
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
	switch (key)
	{
	case 'r': resetCamera(); break;
	case 't': manipulator.reset(); break;
	case 'g': showGui = !showGui; break;
	case 'f': ofToggleFullscreen(); break;
	case 's': ofSaveScreen("screenshot_" + ofGetTimestampString() + ".png"); break;
	}
}
