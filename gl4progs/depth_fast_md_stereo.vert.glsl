#version 420
#extension GL_ARB_shader_storage_buffer_object : require
#extension GL_ARB_explicit_uniform_location : enable
#extension GL_ARB_shader_draw_parameters : require
#extension GL_AMD_vertex_shader_layer : require

layout( location = 0 ) in vec4 position;
layout( location = 15 ) in int drawId;

layout( std140, binding = 0 ) buffer CB0
{
	mat4 modelMatrix[];
};

layout( location = 0 ) uniform mat4 viewProj[2];

void main() {
	vec4 worldPos = modelMatrix[gl_DrawIDARB] * position;
	gl_Position = viewProj[gl_InstanceID] * worldPos;
	gl_Layer = gl_InstanceID;
}