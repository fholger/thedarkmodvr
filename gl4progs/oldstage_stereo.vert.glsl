#version 430
#extension GL_AMD_vertex_shader_layer : require

layout( location = 0 ) in vec4 Position;
layout( location = 1 ) in vec2 TexCoord;
layout( location = 5 ) in vec4 Color;

layout( std140, binding = 0 ) uniform UBO{
	mat4 modelMatrix;
	mat4 textureMatrix;
	vec4 localEyePos[2];
	vec4 colorMul;
	vec4 colorAdd;
	vec4 vertexColor;
	vec4 screenTex;
};

layout( location = 0 ) uniform mat4 viewProj[2];

out VertexOut{
	vec4 color;
	vec4 uv;
	flat vec4 localEyePos;
	flat float screenTex;
} OUT;

void main() {
	vec4 vertColor = vec4( 1, 1, 1, 1 ); //vertexColor;
	if( vertColor.a == 0 )
		vertColor = Color;
	OUT.color = vertColor * colorMul + colorAdd;
	OUT.uv = textureMatrix * vec4( TexCoord, 0, 1 );
	OUT.localEyePos = localEyePos[gl_InstanceID];
	OUT.screenTex = screenTex.x;
	gl_Position = viewProj[gl_InstanceID] * ( modelMatrix * Position );
	gl_Layer = gl_InstanceID;
}