#version 430     

layout (location = 0) in vec4 Position;
layout (location = 1) in vec4 TexCoord;   
layout (location = 2) in vec3 Normal;   
layout (location = 3) in vec3 Tangent2;
layout (location = 4) in vec3 Bitangent;   
layout (location = 5) in vec4 Color;

layout (binding = 0) uniform UBO {
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
    vec4 viewOrigin;   
    vec4 diffuseColor;
    vec4 specularColor;
    vec4 cubic;
};

layout (location = 0) uniform mat4 mvpMatrix;

layout (location = 0) out vec3 out_position;
layout (location = 1) out vec2 out_uvDiffuse;        
layout (location = 2) out vec2 out_uvNormal;        
layout (location = 3) out vec2 out_uvSpecular;       
layout (location = 4) out vec4 out_uvLight; 
layout (location = 5) out mat3 out_tangentSpace; 
layout (location = 8) out vec4 out_color;        
layout (location = 9) out vec4 out_lightOrigin;
layout (location = 10) out vec4 out_viewOrigin;
layout (location = 11) out vec4 out_diffuseColor;
layout (location = 12) out vec4 out_specularColor;
layout (location = 13) out float out_cubic;

void main( void ) {
    // transform vertex position into homogenous clip-space   
	gl_Position = mvpMatrix * Position;
 
	out_position = Position.xyz;

	// normal map texgen   
	out_uvNormal.x = dot( bumpMatrixS, TexCoord );
	out_uvNormal.y = dot( bumpMatrixT, TexCoord ); 
 
	// diffuse map texgen
	out_uvDiffuse.x = dot( diffuseMatrixS, TexCoord );
	out_uvDiffuse.y = dot( diffuseMatrixT, TexCoord );
 
	// specular map texgen
	out_uvSpecular.x = dot( specularMatrixS, TexCoord );
	out_uvSpecular.y = dot( specularMatrixT, TexCoord );
 
	// light projection texgen
	out_uvLight.x = dot( lightProjectionS, Position );   
	out_uvLight.y = dot( lightProjectionT, Position );   
	out_uvLight.z = dot( lightFalloff, Position );   
	out_uvLight.w = dot( lightProjectionQ, Position );   
	
	// construct tangent-bitangent-normal 3x3 matrix   
    vec3 Tangent = cross(Bitangent, Normal);
	out_tangentSpace = mat3( clamp(Tangent2,-1,1), clamp(Bitangent,-1,1), clamp(Normal,-1,1) );
 
	// primary color  
	out_color = (Color * colorModulate) + colorAdd;
    
    // pass through uniforms
    out_lightOrigin = lightOrigin;
    out_viewOrigin = viewOrigin;
    out_diffuseColor = diffuseColor;
    out_specularColor = specularColor;
    out_cubic = cubic.x;
}