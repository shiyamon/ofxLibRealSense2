#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup()
{
	ofSetLogLevel(OF_LOG_NOTICE);
	ofLogToConsole();

	// Point cloud UVs are in colour-image pixel coordinates, so rectangle textures are required.
	ofEnableArbTex();
	ofSetVerticalSync(false);
	ofSetBackgroundColor(0);

	panel.setup("", "settings.xml", 10, 100);
	panel.add(pointSize.set("pointSize", 2.f, 1.f, 10.f));
	panel.add(wireframe.set("wireframe (faces only)", false));

	ofLogNotice(__FUNCTION__) << "Found " << ofxLibRealSense2::getDeviceCount() << " RealSense cameras.";

	camera = std::make_shared<ofxLibRealSense2>();

	ofxLibRealSense2::Settings cameraSettings;
	cameraSettings.enableRGB = true;
	cameraSettings.enableIR = false;
	cameraSettings.enableDepth = true;
	cameraSettings.registerImages = true;
	cameraSettings.usePointCloud = true;
	cameraSettings.pointCloudHasFaces = false; // toggle at runtime with "Point cloud faces"
	cameraSettings.bigPointCloud = false;      // "Colour-res point cloud": 1920 x 1080 samples
	cameraSettings.pointCloudTexCoords = true;

	if (camera->open(0, cameraSettings))
	{
		camera->params.setName("RealSense " + camera->getSerial());
		panel.add(camera->params);
	}

	panel.loadFromFile("settings.xml");

	resetCamera();
}

//--------------------------------------------------------------
void ofApp::resetCamera()
{
	// The point cloud is in metres; it's drawn scaled to millimetres (see draw()), so the
	// default ofEasyCam distance/clip planes are tuned for that. A tiny near clip lets you
	// fly right up to the points without them being culled.
	cam.setNearClip(1.f);
	cam.setFarClip(100000.f);
	cam.setTarget(glm::vec3(0.f, 0.f, -2000.f));
	cam.setDistance(2000.f);
}

//--------------------------------------------------------------
void ofApp::exit()
{
	if (camera)
	{
		camera->close();
	}
}

//--------------------------------------------------------------
void ofApp::update()
{
	if (!camera || !camera->isOpen())
	{
		return;
	}

	camera->update();
	if (camera->isFrameNew() && camera->getPixels().isAllocated())
	{
		if (!texColour.isAllocated())
		{
			texColour.allocate(camera->getPixels());
			// The pixels are tagged OF_PIXELS_RGBA, so openFrameworks uploads them in the right
			// channel order already.
			texColour.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
		}

		texColour.loadData(camera->getPixels());
	}
}

//--------------------------------------------------------------
void ofApp::draw()
{
	if (camera && camera->isOpen())
	{
		ofEnableDepthTest();
		cam.begin();
		{
			ofDrawAxis(100.0f);

			ofPushMatrix();
			{
				// Metres -> millimetres, and flip Y/Z from the camera's space into OF's.
				ofScale(1000.f, -1000.f, -1000.f);

				glPointSize(pointSize);
				ofSetColor(255);
				if (texColour.isAllocated())
				{
					// "Point cloud faces" in the device's gui panel switches between points
					// and a triangulated mesh; the mesh's primitive mode follows it.
					// drawPointCloud (not getPointCloud().draw()) so points get per-vertex UVs.
					const bool drawWireframe = wireframe && camera->getPointCloud().hasIndices();
					texColour.bind();
					camera->drawPointCloud(drawWireframe ? OF_MESH_WIREFRAME : OF_MESH_FILL);
					texColour.unbind();
				}
			}
			ofPopMatrix();
		}
		cam.end();
		ofDisableDepthTest();
	}

	ofPushStyle();
	ofSetColor(255);
	ofDrawBitmapStringHighlight(ofToString(ofGetFrameRate(), 2) + " FPS   [r] reset camera  [g] toggle gui  [f] fullscreen", 10, 20);
	ofPopStyle();

	if (showGui)
	{
		panel.draw();
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
	switch (key)
	{
	case 'r':
		resetCamera();
		break;
	case 'g':
		showGui = !showGui;
		break;
	case 'f':
		ofToggleFullscreen();
		break;
	}
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){

}
