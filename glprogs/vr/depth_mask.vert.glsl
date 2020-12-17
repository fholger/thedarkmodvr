#version 330 core

in vec2 attr_Position;

void main() {
	gl_Position = vec4(attr_Position, -1, 1);
}
