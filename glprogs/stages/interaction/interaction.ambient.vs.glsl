#version 140

#pragma tdm_include "stages/interaction/interaction.common.vs.glsl"     

void main( void ) {     
	interactionProcessVertex();
	var_ViewDirLocal = (params[u_idx].viewOrigin.xyz - var_Position).xyz;
}
