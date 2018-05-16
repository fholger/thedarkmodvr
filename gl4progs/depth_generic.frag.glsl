#version 420
#extension GL_ARB_explicit_uniform_location : enable

layout (binding = 0) uniform sampler2D tex0;

in VertexOut{
	vec2 uv;
	float clipPlaneDist;
	flat vec4 color;
	flat float alphaTest;
} IN;

layout (location = 0) out vec4 fragColor;

void main() {
	if (IN.clipPlaneDist < 0.0)
		discard;
	if (IN.alphaTest < 0)
		fragColor = IN.color;	
	else {
		vec4 tex = texture2D(tex0, IN.uv);
		if (tex.a <= IN.alphaTest)
			discard;
		fragColor = tex*IN.color;
	}
}