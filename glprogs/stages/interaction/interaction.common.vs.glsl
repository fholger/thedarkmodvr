// Contains common formulas for computing interaction.
// Includes: illumination model, fetching surface and light properties
// Excludes: shadows


in vec4 attr_Position;
in vec4 attr_TexCoord;
in vec3 attr_Tangent;
in vec3 attr_Bitangent;
in vec3 attr_Normal;
in vec4 attr_Color;

#pragma tdm_include "stages/interaction/interaction.params.glsl"

out vec3 var_Position;
out vec2 var_TexDiffuse;
out vec2 var_TexNormal;
out vec2 var_TexSpecular;
out vec4 var_TexLight;
out vec4 var_Color;
out mat3 var_TangentBitangentNormalMatrix; 
out vec3 var_LightDirLocal;  
out vec3 var_ViewDirLocal;  

void sendTBN() {
	// construct tangent-bitangent-normal 3x3 matrix   
	var_TangentBitangentNormalMatrix = mat3( clamp(attr_Tangent,-1,1), clamp(attr_Bitangent,-1,1), clamp(attr_Normal,-1,1) );
	var_LightDirLocal = (params[u_idx].lightOrigin.xyz - var_Position).xyz * var_TangentBitangentNormalMatrix;
	var_ViewDirLocal = (params[u_idx].viewOrigin.xyz - var_Position).xyz * var_TangentBitangentNormalMatrix;
}

out vec3 var_WorldLightDir;


void interactionProcessVertex() {
	// transform vertex position into homogenous clip-space
	gl_Position = u_projectionMatrix * (params[u_idx].modelViewMatrix * attr_Position);

	var_Position = attr_Position.xyz;

	// normal map texgen
	var_TexNormal.x = dot(attr_TexCoord, params[u_idx].bumpMatrix[0]);
	var_TexNormal.y = dot(attr_TexCoord, params[u_idx].bumpMatrix[1]);

	// diffuse map texgen
	var_TexDiffuse.x = dot(attr_TexCoord, params[u_idx].diffuseMatrix[0]);
	var_TexDiffuse.y = dot(attr_TexCoord, params[u_idx].diffuseMatrix[1]);

	// specular map texgen
	var_TexSpecular.x = dot(attr_TexCoord, params[u_idx].specularMatrix[0]);
	var_TexSpecular.y = dot(attr_TexCoord, params[u_idx].specularMatrix[1]);

	// light projection texgen
	var_TexLight = ( attr_Position * params[u_idx].lightProjectionFalloff ).xywz;

	// construct tangent-bitangent-normal 3x3 matrix
	sendTBN();

	// primary color
	var_Color = (attr_Color * params[u_idx].colorModulate) + params[u_idx].colorAdd;

	// light->fragment vector in world coordinates
	var_WorldLightDir = (params[u_idx].modelMatrix * vec4(attr_Position.xyz - params[u_idx].lightOrigin.xyz, 1)).xyz;
}
