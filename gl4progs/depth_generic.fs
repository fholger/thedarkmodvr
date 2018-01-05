#version 420
#extension GL_ARB_explicit_uniform_location : enable

layout (location = 4) uniform vec4 color;
layout (location = 5) uniform float alphaTest;
layout (binding = 0) uniform sampler2D tex0;

layout (location = 0) in vec2 uv;
layout (location = 1) in float clipPlaneDist;

layout (location = 0) out vec4 fragColor;

void main() {
	if (clipPlaneDist < 0.0)
		discard;
	if (alphaTest < 0)
		fragColor = color;	
	else {
		vec4 tex = texture2D(tex0, uv);
		if (tex.a <= alphaTest)
			discard;
		fragColor = tex*color;
	}
}