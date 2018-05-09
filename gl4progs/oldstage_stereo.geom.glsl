#version 450

layout( triangles, invocations = 2 ) in;
layout( triangle_strip, max_vertices = 3 ) out;

in VertexOut{
	vec4 color;
	vec4 uv;
	flat vec4 localEyePos[2];
	flat float screenTex;
	vec4 position[2];
} IN[];

out GeomOut{
	vec4 color;
	vec4 uv;
	flat vec4 localEyePos;
	flat float screenTex;
} OUT;

void main() {
	for( int i = 0; i < IN.length(); i++ ) {
		gl_Layer = gl_InvocationID;
		gl_Position = IN[i].position[gl_InvocationID];
		OUT.color = IN[i].color;
		OUT.uv = IN[i].uv;
		OUT.localEyePos = IN[i].localEyePos[gl_InvocationID];
		OUT.screenTex = IN[i].screenTex;
		EmitVertex();
	}
	EndPrimitive();
}
