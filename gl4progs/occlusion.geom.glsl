#version 430

// render the 3 visible sides of the box
layout( points, invocations = 3 ) in;
layout( triangle_strip, max_vertices = 4 ) out;

struct DrawData {
	mat4 modelMatrix;
	vec4 localViewPos;
};

layout( std140, binding = 1 ) buffer CB0{
	DrawData drawData[];
};


in VertexOut{
	vec3 bboxCenter;
	vec3 bboxDimensions;
	flat int id;
} IN[1];

flat out int id;

layout( location = 0 ) uniform vec3 viewPos;
layout( location = 1 ) uniform mat4 viewProjMatrix;

void main() {
	mat4 modelMatrix = drawData[IN[0].id].modelMatrix;

	vec3 faceNormal = vec3( 0 );
	vec3 edgeBasis0 = vec3( 0 );
	vec3 edgeBasis1 = vec3( 0 );

	int invocation = gl_InvocationID;
	if( invocation == 0 ) {
		faceNormal.x = IN[0].bboxDimensions.x;
		edgeBasis0.y = IN[0].bboxDimensions.y;
		edgeBasis1.z = IN[0].bboxDimensions.z;
	} else if( invocation == 1 ) {
		faceNormal.y = IN[0].bboxDimensions.y;
		edgeBasis0.z = IN[0].bboxDimensions.z;
		edgeBasis1.x = IN[0].bboxDimensions.x;
	} else if( invocation == 2 ) {
		faceNormal.z = IN[0].bboxDimensions.z;
		edgeBasis0.x = IN[0].bboxDimensions.x;
		edgeBasis1.y = IN[0].bboxDimensions.y;
	}

	vec3 worldCenter = ( modelMatrix * vec4( IN[0].bboxCenter, 1 ) ).xyz;
	vec3 worldNormal = mat3( modelMatrix ) * faceNormal;
	vec3 worldPos = worldCenter + worldNormal;
	float proj = - sign( dot( worldPos - viewPos.xyz, worldNormal ) );

	faceNormal = mat3( modelMatrix ) * faceNormal * proj;
	edgeBasis0 = mat3( modelMatrix ) * edgeBasis0;
	edgeBasis1 = mat3( modelMatrix ) * edgeBasis1 * proj;

	id = IN[0].id;

	gl_Position = viewProjMatrix * vec4( worldCenter + ( faceNormal - edgeBasis0 - edgeBasis1 ), 1 );
	EmitVertex();
	gl_Position = viewProjMatrix * vec4( worldCenter + ( faceNormal + edgeBasis0 - edgeBasis1 ), 1 );
	EmitVertex();
	gl_Position = viewProjMatrix * vec4( worldCenter + ( faceNormal - edgeBasis0 + edgeBasis1 ), 1 );
	EmitVertex();
	gl_Position = viewProjMatrix * vec4( worldCenter + ( faceNormal + edgeBasis0 + edgeBasis1 ), 1 );
	EmitVertex();
}