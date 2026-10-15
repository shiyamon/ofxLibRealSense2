#pragma once

// ofxLibRealSense2 shader example.
//
// Two shader stages, both taken from the Synk project and given named parameters:
//
// 1. Texture effects (bin/data/shaders/texEffects.*): 2D feedback effects run over the colour
//    image with a ping-pong buffer - each pass reads the previous pass, and effects can also
//    read the previous frame's output. The result becomes the mesh's texture.
// 2. Scene shader (bin/data/shaders/scene.*): draws the triangulated point cloud (filled,
//    wireframe or points) with a colour grade, crop box, vertex wave/scale, and Blinn-Phong
//    lighting from the mesh normals.
//
// Every uniform has its own named GUI control (Shader panel); the Device panel has the
// camera's own settings. [l] reloads the shaders from disk while running.

#include "ofMain.h"

#include "ofxLibRealSense2.h"
#include "ofxGui.h"

// Two FBOs to render back and forth between (from Synk's pingPongBuffer).
struct PingPongBuffer
{
	void allocate(int width, int height, int internalFormat = GL_RGBA)
	{
		for (auto& fbo : fbos)
		{
			fbo.allocate(width, height, internalFormat);
			fbo.getTexture().setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
		}
		clear();
	}

	void swap() { std::swap(src, dst); }

	void clear()
	{
		for (auto& fbo : fbos)
		{
			fbo.begin();
			ofClear(0, 255);
			fbo.end();
		}
	}

	bool isAllocated() const { return fbos[0].isAllocated(); }

	ofFbo* src = &fbos[0]; // read from
	ofFbo* dst = &fbos[1]; // render into

private:
	ofFbo fbos[2];
};

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
	enum RenderMode { FilledMesh, Wireframe, Points };

	void setupGui();
	void loadShaders();
	void resetCamera();
	void runTextureEffects();
	void setTextureEffectUniforms();
	void setSceneUniforms(const glm::mat4& modelView);
	void onTextureEffectChanged(int& mode);
	void onRenderModeChanged(int& mode);
	const ofTexture& meshTexture() const;
	static std::string textureEffectName(int mode);
	static std::string renderModeName(int mode);

	std::shared_ptr<ofxLibRealSense2> camera;
	ofTexture texColour;

	ofShader texEffectShader;
	ofShader sceneShader;
	PingPongBuffer pingPong;
	ofFbo effectOutput; // last frame's effect result (the feedback memory)
	bool effectFrameReady = false;

	ofEasyCam cam;

	ofxPanel shaderPanel;
	ofxPanel devicePanel;
	bool showGui = true;

	// ---- Pipeline ----
	ofParameterGroup pipelineGroup;
	ofParameter<int> textureEffect;
	ofParameter<int> effectPasses;
	ofParameter<bool> useSceneShader;
	ofParameter<int> renderMode;
	ofParameter<int> colourSource;
	ofParameter<float> pointSize;
	ofParameter<ofFloatColor> tint;
	ofParameter<void> clearFeedback;
	ofEventListener clearFeedbackListener;

	// ---- Texture effects (one group per effect) ----
	ofParameterGroup thresholdGroup;
	ofParameter<float> threshold;

	ofParameterGroup decayGroup;
	ofParameter<float> decayDesaturation, decayAmplitude, decaySpeed;

	ofParameterGroup hueGroup;
	ofParameter<ofFloatColor> hueColour;
	ofParameter<float> hueSpeedA, hueSpeedB, hueEdgeRadius;

	ofParameterGroup scanGroup;
	ofParameter<ofFloatColor> scanColour;
	ofParameter<float> scanSpeed;

	ofParameterGroup artifactGroup;
	ofParameter<float> artifactAmount, artifactSpeed;

	ofParameterGroup artifact2Group;
	ofParameter<float> artifact2Distortion;

	ofParameterGroup artifact3Group;
	ofParameter<float> artifact3NoiseSize, artifact3Feedback;

	ofParameterGroup madnessGroup;
	ofParameter<float> madnessSpeed, madnessFeedback;

	// ---- Scene shader ----
	ofParameterGroup vertexGroup;
	ofParameter<glm::vec3> vertexScale;
	ofParameter<bool> waveOn;
	ofParameter<float> waveAmplitude, waveSpeed, waveFrequency;

	ofParameterGroup gradeGroup;
	ofParameter<bool> gradeOn;
	ofParameter<float> gradeEffect, gradeContrast, gradeColourShape;

	ofParameterGroup cropGroup;
	ofParameter<bool> cropOn;
	ofParameter<bool> showCropBox;
	ofParameter<glm::vec3> cropCentre, cropSize;

	ofParameterGroup lightGroup;
	ofParameter<bool> lightingOn;
	ofParameter<bool> showLight;
	ofParameter<float> lightAzimuth, lightElevation;
	ofParameter<ofFloatColor> lightColour;
	ofParameter<float> ambient, diffuse, specular, shininess;

	std::vector<std::string> effectGroupNames;
};
