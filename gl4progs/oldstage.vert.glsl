#version 430     

layout (location = 0) in vec4 Position;
layout (location = 1) in vec2 TexCoord;   
layout (location = 5) in vec4 Color;

layout (std140, binding = 0) uniform UBO {
    mat4 modelMatrix;
    mat4 mvpMatrix;
    mat4 textureMatrix;
    vec4 localEyePos;
    vec4 colorMul;
    vec4 colorAdd;
    vec4 vertexColor;
    vec4 screenTex;
};

layout (location = 0) out vec4 color_out;
layout (location = 1) out vec4 uv_out;
layout (location = 2) flat out vec4 localEyePos_out;
layout (location = 3) flat out float screenTex_out;

void main() {
    vec4 vertColor = vec4(1,1,1,1); //vertexColor;
    if (vertColor.a == 0)
        vertColor = Color;
	color_out = vertColor * colorMul + colorAdd;
	uv_out = textureMatrix * vec4(TexCoord, 0, 1);
    localEyePos_out = localEyePos;
    screenTex_out = screenTex.x;
	gl_Position = mvpMatrix * Position;
}