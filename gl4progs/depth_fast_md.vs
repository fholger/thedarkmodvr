#version 420
#extension GL_ARB_shader_storage_buffer_object : require
#extension GL_ARB_explicit_uniform_location : enable

layout (location = 0) in vec4 position;
layout (location = 15) in int drawId;

layout (std140, binding = 0) buffer CB0
{
    mat4 modelViewMatrix[];
};

layout (location = 0) uniform mat4 projectionMatrix;

void main() {
	gl_Position = projectionMatrix * modelViewMatrix[drawId] * position;
}