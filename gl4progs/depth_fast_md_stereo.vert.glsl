#version 420
#extension GL_ARB_shader_storage_buffer_object : require
#extension GL_ARB_explicit_uniform_location : enable

layout( location = 0 ) in vec4 position;
layout( location = 15 ) in int drawId;

layout( std140, binding = 0 ) buffer CB0
{
	mat4 modelMatrix[];
};

layout( binding = 0 ) uniform mat4 viewProj[2];

out VertexOut{
	vec4 position[2];
} OUT;

void main() {
	vec4 worldPos = modelMatrix[drawId] * position;
	OUT.position[0] = viewProj[0] * worldPos;
	OUT.position[1] = viewProj[1] * worldPos;
}