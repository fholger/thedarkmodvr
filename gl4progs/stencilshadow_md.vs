#version 420
#extension GL_ARB_shader_storage_buffer_object : require
//#extension GL_ARB_shader_draw_parameters : require

layout (location = 0) in vec4 position;
layout (location = 1) in int drawId;

struct DrawData {
	mat4 modelMatrix;
	vec4 lightOrigin;
};
  
layout (std140, binding = 0) buffer CB0
{
    DrawData data[];
};

/*layout (std140, binding = 0) uniform DataBlock {
	DrawData data[512];
};*/

uniform mat4 viewProjectionMatrix;

void main( void ) {
	//int drawId = gl_DrawIDARB;
	vec4 vertex;
	if( position.w == 1.0 ) {
		// mvp transform into clip space     
		vertex = data[drawId].modelMatrix * position;
	} else {
		// project vertex position to infinity
		vertex = data[drawId].modelMatrix * (position - data[drawId].lightOrigin);
	}
	gl_Position = viewProjectionMatrix * vertex;
}