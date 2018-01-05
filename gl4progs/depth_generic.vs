#version 420
#extension GL_ARB_explicit_uniform_location : enable

layout (location = 0) uniform mat4 projectionMatrix;
layout (location = 1) uniform mat4 modelViewMatrix;
layout (location = 2) uniform vec4 clipPlane;
layout (location = 3) uniform mat4 textureMatrix;

layout (location = 0) in vec4 position;
layout (location = 1) in vec2 texCoord;

layout (location = 0) out vec2 uv;
layout (location = 1) out float clipPlaneDist;

void main() {
	uv = (textureMatrix * vec4(texCoord, 0, 1)).st;
	clipPlaneDist = dot(position, clipPlane);
	gl_Position = projectionMatrix * (modelViewMatrix * position);
}