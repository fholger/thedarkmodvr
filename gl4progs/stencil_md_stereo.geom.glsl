#version 450

layout( triangles, invocations = 2 ) in;
layout( triangle_strip, max_vertices = 3 ) out;

in VertexOut{
	vec4 position[2];
} IN[];

void main() {
	for( int i = 0; i < IN.length(); i++ ) {
		gl_Layer = gl_InvocationID;
		gl_Position = IN[i].position[gl_InvocationID];
		EmitVertex();
	}
	EndPrimitive();
}
