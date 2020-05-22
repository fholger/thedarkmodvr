#version 330 core

#pragma tdm_include "stages/interaction/interaction.params.glsl"

in vec4 attr_Position;
in vec4	attr_TexCoord;     
in vec3	attr_Tangent;     
in vec3	attr_Bitangent;     
in vec3	attr_Normal;  
in vec4	attr_Color;
layout (location = 15) in int drawId;
 
out vec3 var_Position;
out vec2 var_TexDiffuse; 
out vec2 var_TexSpecular; 
out vec2 var_TexNormal;
out vec4 var_TexLight; 
out mat3 var_TangentBinormalNormalMatrix;  
out vec4 var_Color; 
out vec3 var_tc0;  
out vec3 var_localViewDir;  
out vec4 var_ClipPosition;
flat out int var_drawId;

void main( void ) {     
    // transform vertex position into homogenous clip-space  
	var_ClipPosition = u_projectionMatrix * (params[drawId].modelViewMatrix * attr_Position);
    gl_Position = var_ClipPosition;
	
	// transform vertex position into world space  
	var_Position = attr_Position.xyz;

	// normal map texgen   
	var_TexNormal.x = dot(attr_TexCoord, params[drawId].bumpMatrix[0]);
	var_TexNormal.y = dot(attr_TexCoord, params[drawId].bumpMatrix[1]);
 
	// diffuse map texgen      
	var_TexDiffuse.x = dot(attr_TexCoord, params[drawId].diffuseMatrix[0]);
	var_TexDiffuse.y = dot(attr_TexCoord, params[drawId].diffuseMatrix[1]);
 
	// specular map texgen  
	var_TexSpecular.x = dot(attr_TexCoord, params[drawId].specularMatrix[0]);
	var_TexSpecular.y = dot(attr_TexCoord, params[drawId].specularMatrix[1]);
 
	// light projection texgen
	var_TexLight = (attr_Position * params[drawId].lightProjectionFalloff).xywz;

	// construct tangent-binormal-normal 3x3 matrix    
	var_TangentBinormalNormalMatrix = mat3( attr_Tangent, attr_Bitangent, attr_Normal ); 
	//var_tc0 = u_lightOrigin.xyz.xyz * var_TangentBinormalNormalMatrix;
	var_localViewDir = (params[drawId].viewOrigin.xyz - var_Position).xyz;

	// primary color 
	var_Color = (attr_Color * params[drawId].colorModulate) + params[drawId].colorAdd;  
    
    var_drawId = drawId;
}