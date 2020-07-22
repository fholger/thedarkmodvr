#version 140
// !!ARBvp1.0 

#pragma tdm_include "tdm_transform.glsl"

INATTR_POSITION  //in vec4 attr_Position;
in vec4 attr_TexCoord;
out vec4 var_tc0;
out vec4 var_tc1;
out vec4 var_tc2;
uniform vec4 u_localParam0;
uniform vec4 u_localParam1;

void main() {
	// OPTION ARB_position_invariant;
	//
	// SteveL #3944: Do not distort where the sampled texel would come from a surface closer than
	// the water surface. Fix for water distorting player weapon etc. Amendments to frag prog.
	//
	// input:
	// 
	// attrib[8] (former texcoord[0]) TEX0 texcoords
	//
	// local[0]	scroll
	// local[1]	deform magnitude (1.0 is reasonable, 2.0 is twice as wavy, 0.5 is half as wavy, etc)
	//
	// output:
	// texCoord[0] is the model surface texture coords unmodified for the mask
	// texCoord[1] is the model surface texture coords with a scroll
	// texCoord[2] is the copied deform magnitude
	
	vec4 R0, R1, R2;                                                                                    //TEMP	R0, R1, R2;
	
	// texture 0 takes the texture coordinates unmodified
	var_tc0 = attr_TexCoord;                                                                            //MOV		result.texcoord[0], vertex.attrib[8];
	
	// texture 1 takes the texture coordinates and adds a scroll
	var_tc1 = (attr_TexCoord) + (u_localParam0);                                                        //ADD		result.texcoord[1], vertex.attrib[8], program.local[0];
	
	// texture 2 takes the deform magnitude and scales it by the projection distance
	vec4 vec = vec4(1, 0, 0, 1);                                                                        //PARAM	vec = { 1, 0, 0, 1 };
	
	R0 = vec;                                                                                           //MOV		R0, vec;
	R0.z = dot(attr_Position, transpose(u_modelViewMatrix)[2]);                                         //DP4		R0.z, vertex.position, state.matrix.modelview.row[2];
	
	R1 = vec4(dot(R0, transpose(u_projectionMatrix)[0]));                                               //DP4		R1, R0, state.matrix.projection.row[0];
	R2 = vec4(dot(R0, transpose(u_projectionMatrix)[3]));                                               //DP4		R2, R0, state.matrix.projection.row[3];
	
	// don't let the recip get near zero for polygons that cross the view plane
	R2 = max(R2, vec4(1));                                                                              //MAX		R2, R2, 1;
	
	R2 = vec4(1.0 / R2.w);                                                                              //RCP		R2, R2.w;
	R1 = (R1) * (R2);                                                                                   //MUL		R1, R1, R2;
	
	// clamp the distance so the the deformations don't get too wacky near the view
	R1 = min(R1, vec4(0.02));                                                                           //MIN		R1, R1, 0.02;
	
	R1 = vec4(0.005);
    var_tc2 = (R1) * (u_localParam1);
	
	gl_Position = tdm_transform(attr_Position);
}
