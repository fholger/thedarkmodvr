#version 430

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

layout( location = 0 ) uniform mat4 viewProjLeft;
layout( location = 1 ) uniform mat4 viewProjRight;

out VertexOut{
	vec4 color;
	vec4 uv;
	flat vec4 localEyePos[2];
	flat float screenTex;
	vec4 position[2];
} OUT;

void main() {
	vec4 vertColor = vec4( 1, 1, 1, 1 ); //vertexColor;
	if( vertColor.a == 0 )
		vertColor = Color;
	OUT.color = vertColor * colorMul + colorAdd;
	OUT.uv = textureMatrix * vec4( TexCoord, 0, 1 );
	OUT.localEyePos[0] = localEyePos[0];
	OUT.localEyePos[1] = localEyePos[1];
	OUT.screenTex = screenTex.x;
	vec4 worldPos = modelMatrix * Position;
	OUT.position[0] = viewProjLeft * worldPos;
	OUT.position[1] = viewProjRight * worldPos;
}