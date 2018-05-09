#version 430

in GeomOut{
	vec4 color;
	vec4 uv;
	flat vec4 localEyePos;
	flat float screenTex;
} IN;

uniform sampler2D tex0;

layout (location = 0) out vec4 fragColor;

void main() {
	vec4 tex;
	if (IN.screenTex == 1) 
		tex = texture2D(tex0, gl_FragCoord.xy/textureSize(tex0, 0));
	else
		tex = textureProj(tex0, IN.uv);
	fragColor = tex*IN.color;
}