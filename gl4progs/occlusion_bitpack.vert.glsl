#version 430

layout( location = 7 ) in uvec4 instream[8];

layout( std430, binding = 0 ) writeonly buffer outputBuffer{
	uint outstream[];
};

void main() {
	uint bits = 0u;
	int outbit = 0;
	for( int i = 0; i < 8; i++ ) {
		for( int n = 0; n < 4; n++, outbit++ ) {
			uint checkbytes = instream[i][n];
			bits |= ( checkbytes & 1u ) << outbit;
		}
	}
	outstream[gl_VertexID] = bits;
}