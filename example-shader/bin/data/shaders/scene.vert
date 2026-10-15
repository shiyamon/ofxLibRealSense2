#version 150

// Point cloud / mesh vertex shader. Positions arrive in the Kinect's camera space (metres,
// +Y down, +Z forward); the app's model matrix converts to display space.
// Based on Synk's sceneShader.vert (FloatVert1/2 = wave, FloatVert4-6 = axis scale), with
// normals passed through for lighting.

uniform mat4 modelViewProjectionMatrix;
uniform mat4 modelViewMatrix;
uniform mat3 uNormalMatrix;   // inverse transpose of modelViewMatrix, from the app

uniform float uTime;
uniform vec3 uVertexScale;
uniform int uWaveOn;
uniform float uWaveAmplitude; // metres
uniform float uWaveSpeed;
uniform float uWaveFrequency; // waves per metre (x)
uniform float uPointSize;
uniform float uHasNormals;

in vec4 position;
in vec3 normal;
in vec2 texcoord;

out vec2 vTexCoord;
out vec3 vDisplayPos;  // metres, display space (+Y up) - used by the crop box
out vec3 vPosEye;
out vec3 vNormalEye;
out float vValid;

void main()
{
	vec3 p = position.xyz;

	// Pixels with no depth return sit at the origin in the dense (triangulated) cloud.
	vValid = p.z > 0.001 ? 1.0 : 0.0;
	vDisplayPos = vec3(p.x, -p.y, -p.z);

	p *= uVertexScale;
	if (uWaveOn == 1)
	{
		// Camera space +Y is down, so subtract to push the wave up on screen.
		p.y -= sin(uTime * uWaveSpeed * 10.0 + p.x * uWaveFrequency) * uWaveAmplitude;
	}

	vec4 eye = modelViewMatrix * vec4(p, 1.0);
	vPosEye = eye.xyz;
	vNormalEye = uHasNormals > 0.5 ? uNormalMatrix * normal : vec3(0.0, 0.0, 1.0);
	vTexCoord = texcoord;

	gl_PointSize = uPointSize;
	gl_Position = modelViewProjectionMatrix * vec4(p, 1.0);
}
