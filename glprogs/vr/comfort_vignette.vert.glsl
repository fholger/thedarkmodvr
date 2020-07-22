#version 330 core

in vec4 attr_Position;
in vec2 attr_TexCoord;

out vec2 var_TexCoord;


void main() {
    var_TexCoord = attr_TexCoord;
	gl_Position = attr_Position;
}
