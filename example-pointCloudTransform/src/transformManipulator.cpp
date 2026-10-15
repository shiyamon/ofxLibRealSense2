#include "transformManipulator.h"

void transformManipulator::setup(const std::string& name, const glm::vec3& defaultPivot)
{
	defaultPivotValue = defaultPivot;

	params.setName(name);
	params.add(translation.set("Translate (m)", glm::vec3(0.f), glm::vec3(-5.f), glm::vec3(5.f)));
	params.add(rotation.set("Rotate (deg)", glm::vec3(0.f), glm::vec3(-180.f), glm::vec3(180.f)));
	params.add(rotationOrder.set("Rotation order XYZ..ZYX", XYZ, XYZ, ZYX));
	params.add(scale.set("Scale", glm::vec3(1.f), glm::vec3(0.05f), glm::vec3(5.f)));
	params.add(uniformScale.set("Uniform scale", 1.f, 0.05f, 5.f));
	params.add(pivot.set("Pivot (m)", defaultPivot, glm::vec3(-5.f), glm::vec3(5.f)));
	params.add(resetButton.set("Reset"));

	listeners.push(translation.newListener([this](glm::vec3&) { onChanged(); }));
	listeners.push(rotation.newListener([this](glm::vec3&) { onChanged(); }));
	listeners.push(rotationOrder.newListener([this](int&) { onChanged(); }));
	listeners.push(scale.newListener([this](glm::vec3&) { onChanged(); }));
	listeners.push(uniformScale.newListener([this](float&) { onChanged(); }));
	listeners.push(pivot.newListener([this](glm::vec3&) { onChanged(); }));
	listeners.push(resetButton.newListener([this]() { reset(); }));
}

void transformManipulator::reset()
{
	translation = glm::vec3(0.f);
	rotation = glm::vec3(0.f);
	rotationOrder = XYZ;
	scale = glm::vec3(1.f);
	uniformScale = 1.f;
	pivot = defaultPivotValue;
}

bool transformManipulator::hasChanged()
{
	const bool c = changed;
	changed = false;
	return c;
}

glm::mat4 transformManipulator::getMatrix() const
{
	const glm::vec3 r = glm::radians(rotation.get());
	const glm::mat4 rx = glm::rotate(glm::mat4(1.f), r.x, glm::vec3(1, 0, 0));
	const glm::mat4 ry = glm::rotate(glm::mat4(1.f), r.y, glm::vec3(0, 1, 0));
	const glm::mat4 rz = glm::rotate(glm::mat4(1.f), r.z, glm::vec3(0, 0, 1));

	// "XYZ" means rotate about X first, then Y, then Z (column vectors: the rightmost applies first).
	glm::mat4 rot;
	switch (rotationOrder.get())
	{
	case XZY: rot = ry * rz * rx; break;
	case YXZ: rot = rz * rx * ry; break;
	case YZX: rot = rx * rz * ry; break;
	case ZXY: rot = ry * rx * rz; break;
	case ZYX: rot = rx * ry * rz; break;
	case XYZ:
	default:  rot = rz * ry * rx; break;
	}

	const glm::mat4 s = glm::scale(glm::mat4(1.f), scale.get() * uniformScale.get());
	const glm::vec3 p = pivot.get();
	return glm::translate(glm::mat4(1.f), translation.get() + p) * rot * s * glm::translate(glm::mat4(1.f), -p);
}
