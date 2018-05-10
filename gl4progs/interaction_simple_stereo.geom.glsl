#version 450

layout( triangles, invocations = 2 ) in;
layout( triangle_strip, max_vertices = 3 ) out;

in VertexOut{
	vec3 position;
	vec2 uvDiffuse;
	vec2 uvNormal;
	vec2 uvSpecular;
	vec4 uvLight;
	mat3 tangentSpace;
	vec4 color;
	vec4 lightOrigin;
	vec4 viewOrigin[2];
	vec4 diffuseColor;
	vec4 specularColor;
	float cubic;
	vec4 worldPos;
} IN[];

layout( location = 0 ) uniform mat4 viewProj[2];

out GeomOut{
	vec3 position;
	vec2 uvDiffuse;
	vec2 uvNormal;
	vec2 uvSpecular;
	vec4 uvLight;
	mat3 tangentSpace;
	vec4 color;
	vec4 lightOrigin;
	vec4 viewOrigin;
	vec4 diffuseColor;
	vec4 specularColor;
	float cubic;
} OUT;


void main() {
	for( int i = 0; i < IN.length(); i++ ) {
		gl_Layer = gl_InvocationID;
		gl_Position = viewProj[gl_InvocationID] * IN[i].worldPos;
		OUT.position = IN[i].position;
		OUT.uvDiffuse = IN[i].uvDiffuse;
		OUT.uvNormal = IN[i].uvNormal;
		OUT.uvSpecular = IN[i].uvSpecular;
		OUT.uvLight = IN[i].uvLight;
		OUT.tangentSpace = IN[i].tangentSpace;
		OUT.color = IN[i].color;
		OUT.lightOrigin = IN[i].lightOrigin;
		OUT.viewOrigin = IN[i].viewOrigin[gl_InvocationID];
		OUT.diffuseColor = IN[i].diffuseColor;
		OUT.specularColor = IN[i].specularColor;
		OUT.cubic = IN[i].cubic;
		EmitVertex();
	}
	EndPrimitive();
}
