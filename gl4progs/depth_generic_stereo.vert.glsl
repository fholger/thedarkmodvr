#version 430
#extension GL_AMD_vertex_shader_layer : require

layout( std140, binding = 0 ) uniform UBO{
	mat4 modelMatrix;
	mat4 textureMatrix;
	vec4 clipPlane;
	vec4 color;
	vec4 alphaTest;
};

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 texCoord;

layout( location = 0 ) uniform mat4 viewProj[2];

out VertexOut{
	vec2 uv;
	float clipPlaneDist;
	flat vec4 color;
	flat float alphaTest;
} OUT;

void main() {
	OUT.uv = ( textureMatrix * vec4( texCoord, 0, 1 ) ).st;
	OUT.clipPlaneDist = dot( position, clipPlane );
	OUT.color = color;
	OUT.alphaTest = alphaTest.x;
	vec4 worldPos = modelMatrix * position;
	gl_Position = viewProj[gl_InstanceID] * position;
	gl_Layer = gl_InstanceID;
}
