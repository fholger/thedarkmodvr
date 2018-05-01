#version 450

layout( triangles, invocations = 2 ) in;
layout( triangle_strip, max_vertices = 3 ) out;

in VertexOut{
	vec2 uv;
	float clipPlaneDist;
	flat vec4 color;
	flat float alphaTest;
	vec4 worldPos;
} IN;

out GeometryOut{
	vec2 uv;
	float clipPlaneDist;
	flat vec4 color;
	flat float alphaTest;
} OUT;

void main() {
	for( int i = 0; i < IN.length(); i++ ) {
		gl_Layer = gl_InvocationID;
		gl_Position = IN.position[gl_InvocationID];
		OUT.uv = IN.uv;
		OUT.clipPlaneDist = IN.clipPlaneDist;
		OUT.color = IN.color;
		OUT.alphaTest = IN.alphaTest;
		EmitVertex();
	}
	EndPrimitive();
}
