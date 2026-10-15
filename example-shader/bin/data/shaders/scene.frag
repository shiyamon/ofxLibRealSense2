#version 150

// Point cloud / mesh fragment shader: Synk's sceneShader.frag colour grade and crop box
// (FloatFrag1-3 now named), plus Blinn-Phong lighting from the mesh normals.

uniform sampler2DRect tex0;   // the camera image, or the texture effect's output
uniform mat4 viewMatrix;

// 0 = camera/effect texture, 1 = normals, 2 = flat white (shows the lighting on its own)
uniform int uColourSource;
uniform vec4 uTint;

uniform int uGradeOn;
uniform float uGradeEffect;       // FloatFrag1 - effect power
uniform float uGradeContrast;     // FloatFrag2
uniform float uGradeColourShape;  // FloatFrag3

uniform int uCropOn;
uniform vec3 uCropCentre;  // metres, display space
uniform vec3 uCropSize;

uniform int uLightingOn;
uniform float uHasNormals;
uniform vec3 uLightDirection;  // display space, pointing towards the light
uniform vec3 uLightColour;
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;

in vec2 vTexCoord;
in vec3 vDisplayPos;
in vec3 vPosEye;
in vec3 vNormalEye;
in float vValid;
out vec4 outputColor;

vec3 EffectShape5(vec3 x, float effect){
	vec4 B = vec4(0.0, 0.0, 1.5, -0.28);
	vec4 G = vec4(0.01, 0.0, 1.03, -0.02);
	vec4 R = vec4(0.0, 0.0, 0.77, 0.1);
	effect *= 4.0/5.0;
	x.r = mix(x, x*x*x*R.x + x*x*R.y + x*R.z + R.w, effect).r;
	x.g = mix(x, x*x*x*G.x + x*x*G.y + x*G.z + G.w, effect).g;
	x.b = mix(x, max(-0.3*x, x*x*x*B.x + x*x*B.y + x*B.z + B.w), effect).b;
	return x;
}

vec3 ColorShape(vec3 x, float color){
	x += (color - 0.5) * vec3(0.0, 0.08, -0.24);
	return x;
}

vec3 ColorContrast(vec3 x, float contrast){
	vec4 k0 = vec4(-1.24, 1.84, 0.4, 0.0);
	x = mix(x, x*x*x*k0.x + x*x*k0.y + x*k0.z + k0.w, contrast*(1.0 + (0.6 - pow(abs(x - 0.5), vec3(0.5)))));
	return x;
}

void main()
{
	if (vValid < 0.5) discard;
	if (uCropOn == 1 && any(greaterThan(abs(vDisplayPos - uCropCentre), uCropSize * 0.5))) discard;

	vec4 col;
	if (uColourSource == 1)      col = vec4(normalize(vNormalEye) * 0.5 + 0.5, 1.0);
	else if (uColourSource == 2) col = vec4(1.0);
	else                         col = texture(tex0, vTexCoord);

	if (uGradeOn == 1)
	{
		for (int i = 0; i < 3; i++)
		{
			col.rgb = ColorShape(col.rgb, uGradeColourShape);
			col.rgb = ColorContrast(col.rgb, uGradeContrast);
		}
		col.rgb = EffectShape5(col.rgb, uGradeEffect);
	}

	if (uLightingOn == 1 && uHasNormals > 0.5)
	{
		vec3 N = normalize(vNormalEye);
		vec3 V = normalize(-vPosEye);
		if (dot(N, V) < 0.0) N = -N;   // light both sides of the surface
		vec3 L = normalize(mat3(viewMatrix) * uLightDirection);
		vec3 H = normalize(L + V);
		float diffuse = max(dot(N, L), 0.0);
		float specular = diffuse > 0.0 ? pow(max(dot(N, H), 0.0), uShininess) : 0.0;
		col.rgb = col.rgb * (uAmbient + uDiffuse * diffuse * uLightColour) + uSpecular * specular * uLightColour;
	}

	outputColor = col * uTint;
}
