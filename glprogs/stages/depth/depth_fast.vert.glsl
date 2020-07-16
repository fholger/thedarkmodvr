
#version 330 core

in vec4 attr_Position;
in int attr_DrawId;

uniform ViewParamsBlock {
	uniform mat4 u_projectionMatrix;
};

#pragma tdm_define "MAX_SHADER_PARAMS"

layout (std140) uniform PerDrawCallParamsBlock {
	mat4 modelViewMatrix[MAX_SHADER_PARAMS];
};

void main() {
	vec4 viewPos = modelViewMatrix[attr_DrawId] * attr_Position;
	gl_Position = u_projectionMatrix * viewPos;
}
