#version 420

layout (early_fragment_tests) in;
layout (location = 0) out vec4 color;
  
void main( void ) {   
	color = vec4(0, 0, 0, 1);
}