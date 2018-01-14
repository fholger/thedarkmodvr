#version 430

layout( std430, binding = 0 ) buffer visibilityBuffer{
	int visible[];
};

struct DrawData {
	mat4 modelMatrix;
	vec4 localViewPos;
};

layout( std140, binding = 1 ) buffer CB0{
	DrawData drawData[];
};

layout( location = 13 ) in vec4 bboxMin;
layout( location = 14 ) in vec4 bboxMax;

out VertexOut{
	vec3 bboxCenter;
	vec3 bboxDimensions;
	flat int id;
} OUT;


void main() {
	int id = gl_VertexID;
	vec3 bboxCenter = ( ( bboxMin + bboxMax )*0.5 ).xyz;
	vec3 bboxDimensions = ( ( bboxMax - bboxMin ) * 0.5 ).xyz;

	// if view origin is inside the bounding box, none of its sides are visible
	// must treat the object as visible, because parts of it might still be rendered
	vec3 distToView = drawData[id].localViewPos.xyz - bboxCenter;
	if( all( lessThan( abs( distToView ), bboxDimensions ) ) ) {
		visible[id] = 1;
	}

	OUT.id = id;
	OUT.bboxCenter = bboxCenter;
	OUT.bboxDimensions = bboxDimensions;
}