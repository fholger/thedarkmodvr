#pragma tdm_include "tdm_transform.glsl"

INATTR_POSITION  //in vec4 attr_Position;
in vec4 attr_TexCoord;
out vec4 var_color;
out vec4 var_tc0;
out vec4 var_tc1;
out vec4 var_tc2;
uniform vec4 u_localParam0;  // scroll
uniform vec4 u_localParam1;  // deform magnitude (1.0 is reasonable, 2.0 is twice as wavy, 0.5 is half as wavy, etc)
uniform vec4 u_localParam2;  // depth of this material needed to completely hide the background. #4306
uniform mat4 u_eyeToHead;

void main() {
	// texture 0 takes the texture coordinates unmodified
	var_tc0 = attr_TexCoord;
	// texture 1 takes the texture coordinates and adds a scroll
	var_tc1 = (attr_TexCoord) + (u_localParam0);
	
	// texture 2 takes the deform magnitude and scales it by the projection distance
    vec4 viewPos = u_modelViewMatrix * attr_Position;
	float viewZ = dot(viewPos, transpose(u_eyeToHead)[2]);
	
	// don't let the recip get near zero for polygons that cross the view plane
	float invDepth = 1.0 / max(1, -viewZ);
	
	// clamp the distance so the the deformations don't get too wacky near the view
	float deform = min(invDepth, 0.02);
	
	//deform = 0.005;
	var_tc2 = deform * u_localParam1;
	
	var_color = u_localParam2;
	gl_Position = tdm_transform(attr_Position);
}
