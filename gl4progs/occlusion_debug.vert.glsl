#version 430

layout( std430, binding = 0 ) writeonly buffer visibilityBuffer{
	int visible[];
};

struct DrawData {
	mat4 modelMatrix;
	vec4 localViewPos;
};

layout( std140, binding = 1 ) readonly buffer CB0{
	DrawData drawData[];
};

layout( location = 12 ) in vec3 bboxMin;
layout( location = 13 ) in vec3 bboxMax;
layout( location = 14 ) in int entityId;

out VertexOut{
	vec3 bboxCenter;
	vec3 bboxDimensions;
	flat int vertexId;
	flat int entityId;
} OUT;


void main() {
	int id = gl_VertexID;
	vec3 bboxCenter = ( bboxMin + bboxMax )*0.5;
	vec3 bboxDimensions = ( bboxMax - bboxMin ) * 0.5 + vec3( 10, 10, 10 );

	// if view origin is inside the bounding box, none of its sides are visible
	// must treat the object as visible, because parts of it might still be rendered
	vec3 distToView = drawData[id].localViewPos.xyz - bboxCenter;
	if( all( lessThan( abs( distToView ), bboxDimensions ) ) ) {
		visible[entityId] = 1;
	}

	OUT.vertexId = id;
	OUT.entityId = entityId;
	OUT.bboxCenter = bboxCenter;
	OUT.bboxDimensions = bboxDimensions;
}