#version 430
#extension GL_ARB_shader_storage_buffer_object : require
#extension GL_ARB_shader_draw_parameters : require
#extension GL_AMD_vertex_shader_layer : require

layout( location = 6 ) in vec4 position;
layout( location = 15 ) in int drawId;

struct DrawData {
	mat4 modelMatrix;
	vec4 lightOrigin;
};

layout( std140, binding = 0 ) buffer CB0{
	DrawData data[];
};

layout( location = 0 ) uniform mat4 viewProj[2];


void main( void ) {
	vec4 projection = ( 1 - position.w ) * data[gl_DrawIDARB].lightOrigin;
	vec4 adjustedPosition = data[gl_DrawIDARB].modelMatrix * (position - projection);
	gl_Position = viewProj[gl_InstanceID] * adjustedPosition;
	gl_Layer = gl_InstanceID;
}