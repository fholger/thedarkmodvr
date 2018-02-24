#version 430

layout( early_fragment_tests ) in;

layout( std430, binding = 0 ) writeonly buffer visibilityBuffer{
	int visible[];
};

flat in int entityId;

layout( location = 0, index = 0 ) out vec4 fragColor;

void main() {
	visible[entityId] = 1;
	fragColor = unpackUnorm4x8( uint( entityId ) );
}
