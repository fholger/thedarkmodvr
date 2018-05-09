#version 430

layout( location = 0 ) in vec4 Position;
layout( location = 1 ) in vec4 TexCoord;
layout( location = 2 ) in vec3 Normal;
layout( location = 3 ) in vec3 Tangent;
layout( location = 4 ) in vec3 Bitangent;
layout( location = 5 ) in vec4 Color;

layout( std140, binding = 0 ) uniform UBO{
	mat4 modelMatrix;
	vec4 bumpMatrixS;
	vec4 bumpMatrixT;
	vec4 diffuseMatrixS;
	vec4 diffuseMatrixT;
	vec4 specularMatrixS;
	vec4 specularMatrixT;
	vec4 lightProjectionS;
	vec4 lightProjectionT;
	vec4 lightProjectionQ;
	vec4 lightFalloff;
	vec4 colorModulate;
	vec4 colorAdd;
	vec4 lightOrigin;
	vec4 viewOrigin[2];
	vec4 diffuseColor;
	vec4 specularColor;
	vec4 cubic;
};

out VertexOut{
	vec3 position;
	vec2 uvDiffuse;
	vec2 uvNormal;
	vec2 uvSpecular;
	vec4 uvLight;
	mat3 tangentSpace;
	vec4 color;
	vec4 lightOrigin;
	vec4 viewOrigin[2];
	vec4 diffuseColor;
	vec4 specularColor;
	float cubic;
	vec4 worldPos;
} OUT;

void main( void ) {
	OUT.position = Position.xyz;
	OUT.worldPos = modelMatrix * Position;

	// normal map texgen   
	OUT.uvNormal.x = dot( bumpMatrixS, TexCoord );
	OUT.uvNormal.y = dot( bumpMatrixT, TexCoord );

	// diffuse map texgen
	OUT.uvDiffuse.x = dot( diffuseMatrixS, TexCoord );
	OUT.uvDiffuse.y = dot( diffuseMatrixT, TexCoord );

	// specular map texgen
	OUT.uvSpecular.x = dot( specularMatrixS, TexCoord );
	OUT.uvSpecular.y = dot( specularMatrixT, TexCoord );

	// light projection texgen
	OUT.uvLight.x = dot( lightProjectionS, Position );
	OUT.uvLight.y = dot( lightProjectionT, Position );
	OUT.uvLight.z = dot( lightFalloff, Position );
	OUT.uvLight.w = dot( lightProjectionQ, Position );

	// construct tangent-bitangent-normal 3x3 matrix   
	OUT.tangentSpace = mat3( clamp( Tangent, -1, 1 ), clamp( Bitangent, -1, 1 ), clamp( Normal, -1, 1 ) );

	// primary color  
	OUT.color = ( Color * colorModulate ) + colorAdd;

	// pass through uniforms
	OUT.lightOrigin = lightOrigin;
	OUT.viewOrigin[0] = viewOrigin[0];
	OUT.viewOrigin[1] = viewOrigin[1];
	OUT.diffuseColor = diffuseColor;
	OUT.specularColor = specularColor;
	OUT.cubic = cubic.x;
}