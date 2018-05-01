#version 430

layout( std140, binding = 0 ) uniform UBO{
	mat4 modelMatrix;
	mat4 textureMatrix;
	vec4 clipPlane;
	vec4 color;
	vec4 alphaTest;
};

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 texCoord;

layout( binding = 0 ) uniform mat4 viewProj[2];

out VertexOut{
	vec2 uv;
	float clipPlaneDist;
	flat vec4 color;
	flat float alphaTest;
	vec4 position[2];
} OUT;

void main() {
	OUT.uv = ( textureMatrix * vec4( texCoord, 0, 1 ) ).st;
	OUT.clipPlaneDist = dot( position, clipPlane );
	OUT.color = color;
	OUT.alphaTest = alphaTest.x;
	vec4 worldPos = modelMatrix * position;
	OUT.position[0] = viewProj[0] * position;
	OUT.position[1] = viewProj[1] * position;
}
