#version 430

layout( early_fragment_tests ) in;

layout( std430, binding = 0 ) buffer visibilityBuffer{
	int visible[];
};

flat in int id;

layout( location = 0, index = 0 ) out vec4 fragColor;

void main() {
	visible[id] = 1;
	fragColor = unpackUnorm4x8( uint( id ) );
}
