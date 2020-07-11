#version 330 core

in vec4 attr_Position;
in vec2 attr_TexCoord;

out vec2 var_TexCoord0;
out vec2 var_TexCoord1;

void main() {
	var_TexCoord0 = attr_TexCoord;
    var_TexCoord1 = attr_TexCoord;
	gl_Position = attr_Position;
}