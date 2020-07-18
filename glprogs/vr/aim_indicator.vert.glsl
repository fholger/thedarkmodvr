#version 330 core

in vec4 attr_Position;
in vec2 attr_TexCoord;

out vec2 var_TexCoord;

uniform mat4 u_mvp;
uniform float u_size;

void main() {
    var_TexCoord = attr_TexCoord;
    vec4 position = attr_Position;
    position.xy *= u_size;
	gl_Position = u_mvp * position;
}
