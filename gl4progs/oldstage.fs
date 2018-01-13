#version 430

layout (location = 0) in vec4 color;
layout (location = 1) in vec4 uv;
layout (location = 2) flat in vec4 localEyePos;
layout (location = 3) flat in float screenTex;

uniform sampler2D tex0;

layout (location = 0) out vec4 fragColor;

void main() {
	vec4 tex;
	if (screenTex == 1) 
		tex = texture2D(tex0, gl_FragCoord.xy/textureSize(tex0, 0));
	else
		tex = textureProj(tex0, uv);
	fragColor = tex*color;
}