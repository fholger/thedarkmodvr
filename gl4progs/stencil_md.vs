#version 430
#extension GL_ARB_shader_storage_buffer_object : require

layout (location = 0) in vec4 position;
layout (location = 15) in int drawId;

struct DrawData {
	mat4 modelMatrix;
	vec4 lightOrigin;
};
  
layout (std140, binding = 0) buffer CB0 {
    DrawData data[];
};

layout (location = 0) uniform mat4 viewProjectionMatrix;

void main( void ) {
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