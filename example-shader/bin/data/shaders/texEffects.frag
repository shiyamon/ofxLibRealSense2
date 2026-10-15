#version 150

// 2D feedback effects on the colour image, ported from Synk's texShader.frag with every
// FloatFragN replaced by a named uniform. Texture coordinates are in pixels (rectangle
// textures, like the point cloud's UVs).
//
//   prevTexture: this pass's input (the camera image on the first pass, then the previous pass)
//   tex0:        the final effect output from the previous frame (feedback memory)

uniform sampler2DRect tex0;
uniform sampler2DRect prevTexture;

uniform int uMode;
uniform float uTime;

// 1 Threshold
uniform float uThreshold;
// 2 Decay
uniform float uDecayDesaturation;
uniform float uDecayAmplitude;
uniform float uDecaySpeed;
// 3 Hue cycle
uniform vec3 uHueColour;
uniform float uHueSpeedA;
uniform float uHueSpeedB;
uniform float uHueEdgeRadius;
// 4 Scan liner
uniform vec3 uScanColour;
uniform float uScanSpeed;
// 5 Digital artifact
uniform float uArtifactAmount;
uniform float uArtifactSpeed;
// 6 Digital artifact II
uniform float uArtifact2Distortion;
// 7 Digital artifact III
uniform float uArtifact3NoiseSize;
uniform float uArtifact3Feedback;
// 8 Colour madness
uniform float uMadnessSpeed;
uniform float uMadnessFeedback;

in vec2 varyingtexcoord;
out vec4 outputColor;

//---- Noise functions

mat2 rot2d(float angle){return mat2(cos(angle),-sin(angle),sin(angle),cos(angle));}
float r(float a, float b){return fract(sin(dot(vec2(a,b),vec2(12.9898,78.233)))*43758.5453);}
float h(float a){return fract(sin(dot(a,dot(12.9898,78.233)))*43758.5453);}

float noise(vec3 x){
	vec3 p  = floor(x);
	vec3 f  = fract(x);
	f       = f*f*(3.0-2.0*f);
	float n = p.x + p.y*57.0 + 113.0*p.z;
	return mix(mix(mix( h(n+0.0), h(n+1.0),f.x),
	               mix( h(n+57.0), h(n+58.0),f.x),f.y),
	           mix(mix( h(n+113.0), h(n+114.0),f.x),
	               mix( h(n+170.0), h(n+171.0),f.x),f.y),f.z);
}

vec3 dnoise2f(vec2 p){
	float i = floor(p.x), j = floor(p.y);
	float u = p.x-i, v = p.y-j;
	float du = 30.*u*u*(u*(u-2.)+1.);
	float dv = 30.*v*v*(v*(v-2.)+1.);
	u=u*u*u*(u*(u*6.-15.)+10.);
	v=v*v*v*(v*(v*6.-15.)+10.);
	float a = r(i,     j    );
	float b = r(i+1.0, j    );
	float c = r(i,     j+1.0);
	float d = r(i+1.0, j+1.0);
	float k0 = a;
	float k1 = b-a;
	float k2 = c-a;
	float k3 = a-b-c+d;
	return vec3(k0 + k1*u + k2*v + k3*u*v,
	            du*(k1 + k3*v),
	            dv*(k2 + k3*u));
}

float fbm(vec2 uv){
	vec2 p = uv;
	float f, dx, dz, w = 0.5;
	f = dx = dz = 0.0;
	for(int i = 0; i < 28; ++i){
		vec3 n = dnoise2f(uv);
		dx += n.y;
		dz += n.z;
		f += w * n.x / (1.0 + dx*dx + dz*dz);
		w *= 0.86;
		uv *= vec2(1.16);
		uv *= rot2d(1.25*noise(vec3(p*0.1, 0.12*uTime))+
		            0.75*noise(vec3(p*0.1, 0.20*uTime)));
	}
	return f;
}

void main (void)
{
	vec2 texCoord = varyingtexcoord;
	vec4 col = texture(prevTexture, texCoord);

	if(uMode == 1){
		// Threshold: keep pixels at or below the brightness threshold, black out the rest.
		float thresh = (col.r+col.g+col.b)/3.0;
		if(!(uThreshold >= thresh))
			col = vec4(0.,0.,0.,1.);
	}

	else if(uMode == 2){
		// Decay: wobbling, desaturated image blended with a zoomed copy of the previous pass.
		vec2 uv = texCoord;
		uv = vec2(uv.x + sin((uv.y*.03)+(uTime * uDecaySpeed * 10.)) * (uDecayAmplitude * 20.),
		          uv.y + sin((uv.x*.02)+(uTime * uDecaySpeed * 10.)) * (uDecayAmplitude * 20.));
		vec3 t = texture(tex0, uv).rgb;
		vec3 gray = vec3(dot(vec3(0.3, 0.59, 0.11), t));
		t = mix(t, gray, uDecayDesaturation);
		col = vec4(t,1.);
		vec4 pre = texture(prevTexture, texCoord*0.95);
		col = (col*0.9)+(pre*0.1);
	}

	else if(uMode == 3){
		// Hue cycle: edges (difference from the local average) lit in two cycling colours.
		// The edge radius is in pixels (the original's 0.007 was sub-pixel on these textures).
		float eps = uHueEdgeRadius;
		vec2 uv = texCoord;
		vec3 t   = texture(prevTexture, uv).rgb;
		vec3 t00 = texture(prevTexture, uv+vec2(-eps,-eps)).rgb;
		vec3 t10 = texture(prevTexture, uv+vec2( eps,-eps)).rgb;
		vec3 t01 = texture(prevTexture, uv+vec2(-eps, eps)).rgb;
		vec3 t11 = texture(prevTexture, uv+vec2( eps, eps)).rgb;
		vec3 tm = (t00+t01+t10+t11)/4.;
		vec3 v; vec3 c;
		t = t-tm;
		t = t*t*t;
		v = 100.*t;

		float g = (tm.x-.3)*5.;
		vec3 col0 = vec3(0.);
		vec3 col1 = sin(uTime * uHueSpeedA * 30.0) * uHueColour;
		vec3 col2 = sin(uTime * uHueSpeedB * 30.0) * uHueColour.brg;
		vec3 col3 = vec3(0.);
		if      (g > 2.) c = mix(col2,col3,g-2.);
		else if (g > 1.) c = mix(col1,col2,g-1.);
		else             c = mix(col0,col1,g);

		c = clamp(c,0.,1.);
		v = clamp(v,0.,1.);
		v = c*(1.-v);
		v = clamp(v,0.,1.);
		if (v==col0) v=col3;
		col = vec4(v,1.);
	}

	else if(uMode == 4){
		// Scan liner: expanding rings keyed off the image's red/green channels.
		vec2 uv = texture(prevTexture, texCoord).rg;
		vec3 mCol = vec3(0);
		float w = 0.2;
		float rr = tan(uTime * uScanSpeed * 10.) * 0.5 + 0.6;
		float c = length(uv);
		mCol += smoothstep(rr+w, rr+w-0.1, c);
		mCol -= smoothstep(rr, rr-0.02, c);
		mCol *= uScanColour * 1.4 * ((c-rr+0.03)/(w-0.01));
		col = vec4(mCol,1.0);
	}

	else if(uMode == 5){
		// Digital artifact: radial ripple displacement.
		vec2 dir = texCoord;
		float dist = distance(texCoord, vec2(5.));
		vec2 offset = dir * (sin(dist * 0.1 + uTime * uArtifactSpeed)) * uArtifactAmount;
		col = texture(prevTexture, texCoord + offset);
	}

	else if(uMode == 6){
		// Digital artifact II: noise-warped lookup.
		col = texture(prevTexture, texCoord * fbm(texCoord * uArtifact2Distortion * 0.05));
	}

	else if(uMode == 7){
		// Digital artifact III: noise divided by the image, mixed with last frame's output.
		vec2 uv = 0.1*texCoord;
		float n = fbm(uv * uArtifact3NoiseSize * 0.5);
		col = vec4(n, n, n, 1.0);
		float dist = distance(texCoord, vec2(.5));
		vec2 offset = uv * (sin(dist * 0.1 + uTime*15.)) * 0.05;
		vec2 mtexCoord = texCoord + offset;
		col = col / texture(prevTexture, mtexCoord);
		vec4 pre = texture(tex0, mtexCoord);
		col = (col*(1.0 - uArtifact3Feedback)) + (pre*uArtifact3Feedback);
	}

	else if(uMode == 8){
		// Colour madness: bright areas flash through cycling channels, with feedback.
		vec3 t = texture(tex0, texCoord).rgb;
		col = vec4(t,1.);
		vec4 pre = texture(prevTexture, texCoord);
		float thresh = (col.r+col.g+col.b)/3.0;
		if(thresh > sin(uTime*uMadnessSpeed))
			col = vec4(col.r*sin(uTime), col.g*sin(uTime*2.), col.b*sin(uTime*5.), 1.0);
		col = (col*(1.0 - uMadnessFeedback)) + (pre*uMadnessFeedback);
	}

	outputColor = vec4(col.rgb, 1.0);
}
