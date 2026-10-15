#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup()
{
	ofSetLogLevel(OF_LOG_NOTICE);
	ofLogToConsole();
	// Point cloud UVs and the effect textures are in pixels, so rectangle textures are needed.
	ofEnableArbTex();
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofSetBackgroundColor(10);

	loadShaders();

	camera = std::make_shared<ofxLibRealSense2>();
	ofxLibRealSense2::Settings settings;
	settings.enableRGB = true;
	settings.enableIR = false;
	settings.enableDepth = true;
	settings.registerImages = true;
	settings.usePointCloud = true;
	settings.pointCloudHasFaces = true; // the triangulated mesh, with normals for lighting

	const bool streaming = camera->open(0, settings);

	setupGui();
	if (streaming)
	{
		camera->params.setName("RealSense " + camera->getSerial());
		devicePanel.add(camera->params);
		devicePanel.loadFromFile("device.xml");
	}

	resetCamera();
}

//--------------------------------------------------------------
void ofApp::setupGui()
{
	shaderPanel.setup("Shaders", "shaders.xml", 10, 90);

	pipelineGroup.setName("Pipeline");
	pipelineGroup.add(textureEffect.set("Tex effect", 0, 0, 8));
	pipelineGroup.add(effectPasses.set("Effect passes", 1, 1, 5));
	pipelineGroup.add(useSceneShader.set("Scene shader", true));
	pipelineGroup.add(renderMode.set("Render mode", FilledMesh, FilledMesh, Points));
	pipelineGroup.add(colourSource.set("Colour source", 0, 0, 2));
	pipelineGroup.add(pointSize.set("Point size", 2.f, 1.f, 10.f));
	pipelineGroup.add(tint.set("Tint", ofFloatColor(1.f), ofFloatColor(0.f, 0.f), ofFloatColor(1.f)));
	pipelineGroup.add(clearFeedback.set("Clear feedback"));
	shaderPanel.add(pipelineGroup);

	// Texture effects, numbered as in the "Texture effect" slider.
	thresholdGroup.setName("1 Threshold");
	thresholdGroup.add(threshold.set("Threshold", 0.5f, 0.f, 1.f));

	decayGroup.setName("2 Decay");
	decayGroup.add(decayDesaturation.set("Desaturation", 0.6f, 0.f, 1.f));
	decayGroup.add(decayAmplitude.set("Wobble amplitude", 0.2f, 0.f, 1.f));
	decayGroup.add(decaySpeed.set("Wobble speed", 0.1f, 0.f, 1.f));

	hueGroup.setName("3 Hue cycle");
	hueGroup.add(hueColour.set("Colour", ofFloatColor(1.f, 0.4f, 0.1f), ofFloatColor(0.f, 0.f), ofFloatColor(1.f)));
	hueGroup.add(hueSpeedA.set("Speed A", 0.05f, 0.f, 1.f));
	hueGroup.add(hueSpeedB.set("Speed B", 0.08f, 0.f, 1.f));
	hueGroup.add(hueEdgeRadius.set("Edge radius (px)", 1.5f, 0.5f, 8.f));

	scanGroup.setName("4 Scan liner");
	scanGroup.add(scanColour.set("Colour", ofFloatColor(0.2f, 0.8f, 1.f), ofFloatColor(0.f, 0.f), ofFloatColor(1.f)));
	scanGroup.add(scanSpeed.set("Speed", 0.05f, 0.f, 1.f));

	artifactGroup.setName("5 Digital artifact");
	artifactGroup.add(artifactAmount.set("Amount", 0.05f, 0.f, 0.2f));
	artifactGroup.add(artifactSpeed.set("Speed", 15.f, 0.f, 50.f));

	artifact2Group.setName("6 Digital artifact II");
	artifact2Group.add(artifact2Distortion.set("Distortion", 0.5f, 0.f, 1.f));

	artifact3Group.setName("7 Digital artifact III");
	artifact3Group.add(artifact3NoiseSize.set("Noise size", 0.5f, 0.f, 1.f));
	artifact3Group.add(artifact3Feedback.set("Feedback mix", 0.7f, 0.f, 1.f));

	madnessGroup.setName("8 Colour madness");
	madnessGroup.add(madnessSpeed.set("Speed", 0.5f, 0.f, 5.f));
	madnessGroup.add(madnessFeedback.set("Feedback mix", 0.2f, 0.f, 1.f));

	for (auto* group : { &thresholdGroup, &decayGroup, &hueGroup, &scanGroup, &artifactGroup, &artifact2Group, &artifact3Group, &madnessGroup })
	{
		shaderPanel.add(*group);
		effectGroupNames.push_back(group->getName());
	}

	// Scene shader.
	vertexGroup.setName("Scene: vertex");
	vertexGroup.add(vertexScale.set("Axis scale", glm::vec3(1.f), glm::vec3(0.1f), glm::vec3(3.f)));
	vertexGroup.add(waveOn.set("Wave", false));
	vertexGroup.add(waveAmplitude.set("Wave amplitude (m)", 0.05f, 0.f, 0.5f));
	vertexGroup.add(waveSpeed.set("Wave speed", 0.3f, 0.f, 2.f));
	vertexGroup.add(waveFrequency.set("Waves per metre", 10.f, 0.f, 50.f));
	shaderPanel.add(vertexGroup);

	gradeGroup.setName("Scene: colour grade");
	gradeGroup.add(gradeOn.set("Colour grade", false));
	gradeGroup.add(gradeEffect.set("Effect power", 0.6f, 0.f, 1.f));
	gradeGroup.add(gradeContrast.set("Contrast", 0.2f, 0.f, 1.f));
	gradeGroup.add(gradeColourShape.set("Colour shape", 0.5f, 0.f, 1.f));
	shaderPanel.add(gradeGroup);

	cropGroup.setName("Scene: crop box");
	cropGroup.add(cropOn.set("Crop", false));
	cropGroup.add(showCropBox.set("Show box", true));
	cropGroup.add(cropCentre.set("Centre (m)", glm::vec3(0.f, 0.f, -2.f), glm::vec3(-5.f), glm::vec3(5.f)));
	cropGroup.add(cropSize.set("Size (m)", glm::vec3(3.f), glm::vec3(0.1f), glm::vec3(10.f)));
	shaderPanel.add(cropGroup);

	lightGroup.setName("Scene: lighting");
	lightGroup.add(lightingOn.set("Lighting (needs faces)", true));
	lightGroup.add(showLight.set("Show light direction", false));
	lightGroup.add(lightAzimuth.set("Light azimuth (deg)", 35.f, -180.f, 180.f));
	lightGroup.add(lightElevation.set("Light elevation (deg)", 40.f, -90.f, 90.f));
	lightGroup.add(lightColour.set("Light colour", ofFloatColor(1.f, 0.97f, 0.9f), ofFloatColor(0.f, 0.f), ofFloatColor(1.f)));
	lightGroup.add(ambient.set("Ambient", 0.35f, 0.f, 1.f));
	lightGroup.add(diffuse.set("Diffuse", 0.8f, 0.f, 2.f));
	lightGroup.add(specular.set("Specular", 0.25f, 0.f, 2.f));
	lightGroup.add(shininess.set("Shininess", 24.f, 1.f, 128.f));
	shaderPanel.add(lightGroup);

	textureEffect.addListener(this, &ofApp::onTextureEffectChanged);
	renderMode.addListener(this, &ofApp::onRenderModeChanged);
	clearFeedbackListener = clearFeedback.newListener([this]() {
		if (!pingPong.isAllocated()) return;
		pingPong.clear();
		effectOutput.begin();
		ofClear(0, 255);
		effectOutput.end();
	});

	shaderPanel.loadFromFile("shaders.xml");
	int mode = textureEffect;
	onTextureEffectChanged(mode);
	// Keep the panel on screen: the scene groups start collapsed (click + to open).
	for (auto* group : { &vertexGroup, &gradeGroup, &cropGroup })
	{
		shaderPanel.getGroup(group->getName()).minimize();
	}

	devicePanel.setup("Device", "device.xml", ofGetWidth() - 230, 90);
}

//--------------------------------------------------------------
void ofApp::onTextureEffectChanged(int& mode)
{
	// Only the active effect's controls stay open.
	for (std::size_t i = 0; i < effectGroupNames.size(); i++)
	{
		auto& group = shaderPanel.getGroup(effectGroupNames[i]);
		if (int(i) + 1 == mode) group.maximize();
		else group.minimize();
	}
}

//--------------------------------------------------------------
void ofApp::onRenderModeChanged(int& mode)
{
	// Filled and wireframe meshes need triangles; points work on either cloud.
	if (camera && mode != Points) camera->pointCloudHasFaces = true;
}

//--------------------------------------------------------------
void ofApp::loadShaders()
{
	const bool texOk = texEffectShader.load("shaders/texEffects.vert", "shaders/texEffects.frag");
	const bool sceneOk = sceneShader.load("shaders/scene.vert", "shaders/scene.frag");
	ofLogNotice(__FUNCTION__) << "Texture effect shader " << (texOk ? "loaded" : "FAILED") << ", scene shader " << (sceneOk ? "loaded" : "FAILED");
}

//--------------------------------------------------------------
void ofApp::resetCamera()
{
	// The cloud is in metres and drawn scaled to millimetres; frame it from 2.5m back with
	// clip planes that don't cull it when flying close.
	cam.setNearClip(1.f);
	cam.setFarClip(100000.f);
	cam.setTarget(glm::vec3(0.f, 0.f, -2000.f));
	cam.setDistance(2500.f);
}

//--------------------------------------------------------------
void ofApp::exit()
{
	if (camera) camera->close();
}

//--------------------------------------------------------------
void ofApp::update()
{
	if (!camera || !camera->isOpen()) return;

	camera->update();
	if (!camera->isFrameNew() || !camera->getPixels().isAllocated()) return;

	const auto& colourPix = camera->getPixels();
	if (!texColour.isAllocated())
	{
		// Tagged OF_PIXELS_RGBA, so openFrameworks uploads it in the right channel order.
		texColour.allocate(colourPix);
		const int w = int(colourPix.getWidth()), h = int(colourPix.getHeight());
		pingPong.allocate(w, h, GL_RGBA);
		effectOutput.allocate(w, h, GL_RGBA);
		effectOutput.begin();
		ofClear(0, 255);
		effectOutput.end();
	}
	texColour.loadData(colourPix);

	if (textureEffect > 0 && texEffectShader.isLoaded())
	{
		runTextureEffects();
	}
}

//--------------------------------------------------------------
void ofApp::runTextureEffects()
{
	// Pass input: the camera image.
	pingPong.clear();
	pingPong.src->begin();
	ofSetColor(255);
	texColour.draw(0, 0);
	pingPong.src->end();

	for (int i = 0; i < effectPasses; i++)
	{
		pingPong.dst->begin();
		texEffectShader.begin();
		texEffectShader.setUniformTexture("prevTexture", pingPong.src->getTexture(), 0);
		texEffectShader.setUniformTexture("tex0", effectOutput.getTexture(), 1);
		setTextureEffectUniforms();
		pingPong.src->draw(0, 0);
		texEffectShader.end();
		pingPong.dst->end();
		pingPong.swap();
	}

	// After the last swap, src holds the newest result: keep it as next frame's feedback.
	effectOutput.begin();
	ofClear(0, 255);
	pingPong.src->draw(0, 0);
	effectOutput.end();
	effectFrameReady = true;
}

//--------------------------------------------------------------
void ofApp::setTextureEffectUniforms()
{
	auto& s = texEffectShader;
	s.setUniform1i("uMode", textureEffect);
	s.setUniform1f("uTime", ofGetElapsedTimef());

	s.setUniform1f("uThreshold", threshold);

	s.setUniform1f("uDecayDesaturation", decayDesaturation);
	s.setUniform1f("uDecayAmplitude", decayAmplitude);
	s.setUniform1f("uDecaySpeed", decaySpeed);

	const ofFloatColor hc = hueColour;
	s.setUniform3f("uHueColour", hc.r, hc.g, hc.b);
	s.setUniform1f("uHueSpeedA", hueSpeedA);
	s.setUniform1f("uHueSpeedB", hueSpeedB);
	s.setUniform1f("uHueEdgeRadius", hueEdgeRadius);

	const ofFloatColor sc = scanColour;
	s.setUniform3f("uScanColour", sc.r, sc.g, sc.b);
	s.setUniform1f("uScanSpeed", scanSpeed);

	s.setUniform1f("uArtifactAmount", artifactAmount);
	s.setUniform1f("uArtifactSpeed", artifactSpeed);
	s.setUniform1f("uArtifact2Distortion", artifact2Distortion);
	s.setUniform1f("uArtifact3NoiseSize", artifact3NoiseSize);
	s.setUniform1f("uArtifact3Feedback", artifact3Feedback);

	s.setUniform1f("uMadnessSpeed", madnessSpeed);
	s.setUniform1f("uMadnessFeedback", madnessFeedback);
}

//--------------------------------------------------------------
const ofTexture& ofApp::meshTexture() const
{
	if (textureEffect > 0 && effectFrameReady) return effectOutput.getTexture();
	return texColour;
}

//--------------------------------------------------------------
void ofApp::setSceneUniforms(const glm::mat4& modelView)
{
	auto& s = sceneShader;
	const bool hasNormals = camera->getPointCloud().hasNormals();

	s.setUniformTexture("tex0", meshTexture(), 0);
	s.setUniformMatrix3f("uNormalMatrix", glm::inverseTranspose(glm::mat3(modelView)));
	s.setUniform1f("uTime", ofGetElapsedTimef());
	s.setUniform1f("uHasNormals", hasNormals ? 1.f : 0.f);
	s.setUniform1f("uPointSize", pointSize);
	s.setUniform1i("uColourSource", colourSource);
	const ofFloatColor t = tint;
	s.setUniform4f("uTint", t.r, t.g, t.b, t.a);

	s.setUniform3f("uVertexScale", vertexScale);
	s.setUniform1i("uWaveOn", waveOn ? 1 : 0);
	s.setUniform1f("uWaveAmplitude", waveAmplitude);
	s.setUniform1f("uWaveSpeed", waveSpeed);
	s.setUniform1f("uWaveFrequency", waveFrequency);

	s.setUniform1i("uGradeOn", gradeOn ? 1 : 0);
	s.setUniform1f("uGradeEffect", gradeEffect);
	s.setUniform1f("uGradeContrast", gradeContrast);
	s.setUniform1f("uGradeColourShape", gradeColourShape);

	s.setUniform1i("uCropOn", cropOn ? 1 : 0);
	s.setUniform3f("uCropCentre", cropCentre);
	s.setUniform3f("uCropSize", cropSize);

	const float az = ofDegToRad(lightAzimuth), el = ofDegToRad(lightElevation);
	const glm::vec3 lightDir(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
	const ofFloatColor lc = lightColour;
	s.setUniform1i("uLightingOn", lightingOn ? 1 : 0);
	s.setUniform3f("uLightDirection", lightDir);
	s.setUniform3f("uLightColour", lc.r, lc.g, lc.b);
	s.setUniform1f("uAmbient", ambient);
	s.setUniform1f("uDiffuse", diffuse);
	s.setUniform1f("uSpecular", specular);
	s.setUniform1f("uShininess", shininess);
}

//--------------------------------------------------------------
void ofApp::draw()
{
	devicePanel.setPosition(ofGetWidth() - devicePanel.getWidth() - 10, 90);

	const bool haveCloud = camera && camera->isOpen() && texColour.isAllocated();

	ofEnableDepthTest();
	cam.begin();
	if (haveCloud)
	{
		ofPushMatrix();
		// Metres -> millimetres, and flip Y/Z from the camera's space into OF's.
		ofScale(1000.f, -1000.f, -1000.f);

		const auto& cloud = camera->getPointCloud();
		const ofPolyRenderMode polyMode = renderMode == Wireframe ? OF_MESH_WIREFRAME : (renderMode == Points ? OF_MESH_POINTS : OF_MESH_FILL);

		if (useSceneShader && sceneShader.isLoaded())
		{
			glEnable(GL_PROGRAM_POINT_SIZE); // gl_PointSize from the shader
			sceneShader.begin();
			setSceneUniforms(ofGetCurrentMatrix(OF_MATRIX_MODELVIEW));
			cloud.draw(polyMode);
			sceneShader.end();
			glDisable(GL_PROGRAM_POINT_SIZE);
		}
		else
		{
			glPointSize(pointSize);
			ofSetColor(255);
			meshTexture().bind();
			camera->drawPointCloud(polyMode);
			meshTexture().unbind();
		}
		ofPopMatrix();

		ofPushMatrix();
		ofScale(1000.f);
		if (cropOn && showCropBox)
		{
			ofNoFill();
			ofSetColor(255, 200, 0);
			ofDrawBox(cropCentre.get(), cropSize->x, cropSize->y, cropSize->z);
			ofFill();
		}
		if (showLight)
		{
			const float az = ofDegToRad(lightAzimuth), el = ofDegToRad(lightElevation);
			const glm::vec3 dir(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
			const glm::vec3 centre = cropCentre.get();
			ofSetColor(lightColour.get());
			ofDrawArrow(centre + dir * 1.5f, centre + dir * 0.3f, 0.04f);
		}
		ofPopMatrix();
	}
	cam.end();
	ofDisableDepthTest();

	// Small preview of the texture the mesh is using.
	if (haveCloud && textureEffect > 0 && effectFrameReady)
	{
		const float w = 320.f, h = w * meshTexture().getHeight() / meshTexture().getWidth();
		ofSetColor(255);
		meshTexture().draw(ofGetWidth() - w - 10, ofGetHeight() - h - 40, w, h);
	}

	static const char* colourSources[] = { "camera/effect", "normals", "white" };
	std::string info = ofToString(ofGetFrameRate(), 1) + " fps   Texture effect " + ofToString(textureEffect.get()) + ": " + textureEffectName(textureEffect) +
		"   Render " + ofToString(renderMode.get()) + ": " + renderModeName(renderMode) +
		"   Colour " + ofToString(colourSource.get()) + ": " + colourSources[std::min(std::max(colourSource.get(), 0), 2)] +
		"   Scene shader: " + (useSceneShader ? "on" : "off");
	if (haveCloud && lightingOn && !camera->getPointCloud().hasNormals())
	{
		info += "   (lighting needs Point cloud faces)";
	}
	if (!haveCloud) info += "\nNo RealSense camera streaming.";
	ofDrawBitmapStringHighlight(info, 10, 20);
	ofDrawBitmapStringHighlight("[1-3] fill/wire/points  [ ] prev/next effect  [0] effect off  [c] colour source  [l] reload shaders  [r] reset view  [g] gui  [f] fullscreen  [s] screenshot", 10, ofGetHeight() - 12);

	if (showGui)
	{
		shaderPanel.draw();
		devicePanel.draw();
	}
}

//--------------------------------------------------------------
std::string ofApp::textureEffectName(int mode)
{
	static const char* names[] = { "off", "threshold", "decay", "hue cycle", "scan liner", "digital artifact", "digital artifact II", "digital artifact III", "colour madness" };
	return (mode >= 0 && mode <= 8) ? names[mode] : "?";
}

//--------------------------------------------------------------
std::string ofApp::renderModeName(int mode)
{
	switch (mode)
	{
	case FilledMesh: return "filled mesh";
	case Wireframe: return "wireframe";
	case Points: return "points";
	default: return "?";
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
	switch (key)
	{
	case '1': renderMode = FilledMesh; break;
	case '2': renderMode = Wireframe; break;
	case '3': renderMode = Points; break;
	case '0': textureEffect = 0; break;
	case '[': textureEffect = (textureEffect + 8) % 9; break;
	case ']': textureEffect = (textureEffect + 1) % 9; break;
	case 'c': colourSource = (colourSource + 1) % 3; break;
	case 'l': loadShaders(); break;
	case 'r': resetCamera(); break;
	case 'g': showGui = !showGui; break;
	case 'f': ofToggleFullscreen(); break;
	case 's': ofSaveScreen("screenshot_" + ofGetTimestampString() + ".png"); break;
	}
}
