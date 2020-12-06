#version 140
// !!ARBfp1.0 

in vec4 var_tc0;
in vec4 var_tc1;
in vec4 var_tc2;
in vec4 var_tc3;
in vec4 var_tc4;
out vec4 draw_Color;
uniform sampler2D u_normalTexture;
uniform samplerCube u_texture0;

void main() {
	// OPTION ARB_precision_hint_fastest;
	
	// per-pixel cubic reflextion map calculation
	
	// texture 0 is the environment cube map
	// texture 1 is the normal map
	//
	// texCoord[0] is the normal map texcoord
	// texCoord[1] is the vector to the eye in global space
	// texCoord[3] is the surface tangent to global coordiantes
	// texCoord[4] is the surface bitangent to global coordiantes
	// texCoord[5] is the surface normal to global coordiantes
	
	vec4 globalEye, localNormal, globalNormal, R0, R1;                                                  //TEMP	globalEye, localNormal, globalNormal, R0, R1;
	
	vec4 subOne = vec4(-1, -1, -1, -1);                                                                 //PARAM	subOne			= { -1, -1, -1, -1 };
	vec4 scaleTwo = vec4(2, 2, 2, 2);                                                                   //PARAM	scaleTwo		= { 2, 2, 2, 2 };
	vec4 rimStrength = vec4(3.0, 0.4, 0.0, 1.0);                                                        //PARAM	rimStrength		= {3.0, 0.4, 0.0 };
	
	// load the filtered normal map, then normalize to full scale,
	localNormal = texture(u_normalTexture, var_tc0.xy);                                                 //TEX		localNormal, fragment.texcoord[0], texture[1], 2D;
//	localNormal.x = localNormal.a;                                                                      //MOV		localNormal.x, localNormal.a;				
	localNormal = (localNormal) * (scaleTwo) + (subOne);                                                //MAD		localNormal, localNormal, scaleTwo, subOne;
	localNormal.z = sqrt(max(0, 1-localNormal.x*localNormal.x-localNormal.y*localNormal.y));
	R1 = vec4(dot(localNormal.xyz, localNormal.xyz));                                                   //DP3		R1, localNormal,localNormal;
	R1 = vec4(1.0 / sqrt(R1.x));                                                                        //RSQ		R1, R1.x;
	localNormal.xyz = (localNormal.xyz) * (R1.xyz);                                                     //MUL		localNormal.xyz, localNormal, R1;
	
	// transform the surface normal by the local tangent space 
	globalNormal.x = dot(localNormal.xyz, var_tc2.xyz);                                                 //DP3		globalNormal.x, localNormal, fragment.texcoord[2];
	globalNormal.y = dot(localNormal.xyz, var_tc3.xyz);                                                 //DP3		globalNormal.y, localNormal, fragment.texcoord[3];
	globalNormal.z = dot(localNormal.xyz, var_tc4.xyz);                                                 //DP3		globalNormal.z, localNormal, fragment.texcoord[4];
	globalNormal.w = 0.0;   //stgatilov: avoid uninitialized warning
	
	// normalize vector to eye
	R0 = vec4(dot(var_tc1.xyz, var_tc1.xyz));                                                           //DP3		R0, fragment.texcoord[1], fragment.texcoord[1];
	R0 = vec4(1.0 / sqrt(R0.x));                                                                        //RSQ		R0, R0.x;
	globalEye = (var_tc1) * (R0);                                                                       //MUL		globalEye, fragment.texcoord[1], R0;
	
	// calculate reflection vector
	R0 = vec4(dot(globalEye.xyz, globalNormal.xyz));                                                    //DP3		R0, globalEye, globalNormal;
	//-----------------------------------------
	//		Calculate fresnel reflectance.
	//-----------------------------------------
	R1.x = (1) - (R0.x);                                                                                //SUB		R1.x, 1, R0.x;
	R1.x = (R1.x) * (R1.x);                                                                             //MUL		R1.x, R1.x, R1.x;
	R1.x = (R1.x) * (R1.x);                                                                             //MUL		R1.x, R1.x, R1.x;
	R1.x = (R1.x) * (rimStrength.x);                                                                    //MUL		R1.x, R1.x, rimStrength.x;
	//-----------------------------------------
	
	R0 = (R0) * (globalNormal);                                                                         //MUL		R0, R0, globalNormal;
	R0 = (R0) * (scaleTwo) + (-globalEye);                                                              //MAD		R0, R0, scaleTwo, -globalEye;
	
	// read the environment map with the reflection vector
	R0 = texture(u_texture0, R0.xyz);                                                                   //TEX		R0, R0, texture[0], CUBE;
	
	R1 = (R1) + (rimStrength.yyyy);                                                                     //ADD		R1, R1, rimStrength.y;
	R0 = (R0) * (R1.xxxx);                                                                              //MUL		R0, R0, R1.x;
	
	//---------------------------------------------------------
	// Tone Map to convert HDR values to range 0.0 - 1.0
	//---------------------------------------------------------
	R1 = (vec4(1.0)) + (R0);                                                                            //ADD		R1, 1.0, R0;
	R1.x = 1.0 / R1.x;                                                                                  //RCP		R1.x, R1.x;
	R1.y = 1.0 / R1.y;                                                                                  //RCP		R1.y, R1.y;
	R1.z = 1.0 / R1.z;                                                                                  //RCP		R1.z, R1.z;
	//---------------------------------------------------------
	
	//MUL		result.color.xyz, R0, fragment.color;
	//MOV		result.color.xyz, R0;
	draw_Color.xyz = (R0.xyz) * (R1.xyz);                                                               //MUL		result.color.xyz, R0, R1;
	
}
