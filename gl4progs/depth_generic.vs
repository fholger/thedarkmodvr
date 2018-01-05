#version 430

layout (binding = 0) uniform UBO {
    mat4 modelViewMatrix;
    mat4 textureMatrix;
    vec4 clipPlane;
    vec4 color;
    vec4 alphaTest;
};

layout (location = 0) uniform mat4 projectionMatrix;

layout (location = 0) in vec4 position;
layout (location = 1) in vec2 texCoord;

layout (location = 0) out vec2 uv_out;
layout (location = 1) out float clipPlaneDist_out;
layout (location = 2) flat out vec4 color_out;
layout (location = 3) flat out float alphaTest_out;

void main() {
	uv_out = (textureMatrix * vec4(texCoord, 0, 1)).st;
	clipPlaneDist_out = dot(position, clipPlane);
    color_out = color;
    alphaTest_out = alphaTest.x;
	gl_Position = projectionMatrix * (modelViewMatrix * position);
}