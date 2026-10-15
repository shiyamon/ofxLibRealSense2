#pragma once

#include "ofMain.h"

// A GUI-driven rigid + scale transform: translate, rotate (Euler, selectable order) and scale
// on any axis, all around a pivot point, so rotating/scaling spins the cloud in place instead
// of swinging it around the sensor's origin.
//
// The matrix is  T(translation) * T(pivot) * R * S * T(-pivot)  in glm's column-vector
// convention, i.e. a point is scaled, then rotated, about the pivot, then moved.
class transformManipulator
{
public:
	enum RotationOrder { XYZ, XZY, YXZ, YZX, ZXY, ZYX };

	void setup(const std::string& name, const glm::vec3& defaultPivot = glm::vec3(0.f));

	glm::mat4 getMatrix() const;
	const glm::vec3& getPivot() const { return pivot.get(); }

	// True once after any parameter changed (so the matrix only needs pushing when it does).
	bool hasChanged();

	void reset();

	ofParameterGroup params;
	ofParameter<glm::vec3> translation;   // metres
	ofParameter<glm::vec3> rotation;      // degrees about X, Y, Z
	ofParameter<int> rotationOrder;       // RotationOrder
	ofParameter<glm::vec3> scale;         // per axis
	ofParameter<float> uniformScale;      // multiplies all three axes
	ofParameter<glm::vec3> pivot;         // metres
	ofParameter<void> resetButton;

private:
	void onChanged() { changed = true; }
	glm::vec3 defaultPivotValue;
	bool changed = true;
	ofEventListeners listeners;
};
