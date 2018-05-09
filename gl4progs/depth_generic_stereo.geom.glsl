#version 450

layout( triangles, invocations = 2 ) in;
layout( triangle_strip, max_vertices = 3 ) out;

in VertexOut{
	vec2 uv;
	float clipPlaneDist;
	flat vec4 color;
	flat float alphaTest;
	vec4 position[2];
} IN[];

out GeometryOut{
	vec2 uv;
	float clipPlaneDist;
	flat vec4 color;
	flat float alphaTest;
} OUT;

void main() {
	for( int i = 0; i < IN.length(); i++ ) {
		gl_Layer = gl_InvocationID;
		gl_Position = IN[i].position[gl_InvocationID];
		OUT.uv = IN[i].uv;
		OUT.clipPlaneDist = IN[i].clipPlaneDist;
		OUT.color = IN[i].color;
		OUT.alphaTest = IN[i].alphaTest;
		EmitVertex();
	}
	EndPrimitive();
}
