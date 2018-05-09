#version 430
#extension GL_ARB_shader_storage_buffer_object : require

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

out VertexOut{
	vec4 position[2];
} OUT;

void main( void ) {
	vec4 projection = ( 1 - position.w ) * data[drawId].lightOrigin;
	vec4 adjustedPosition = data[drawId].modelMatrix * (position - projection);
	OUT.position[0] = viewProj[0] * adjustedPosition;
	OUT.position[1] = viewProj[1] * adjustedPosition;
}