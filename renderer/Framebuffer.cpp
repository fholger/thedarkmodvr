#include "precompiled_engine.h"
#include "Framebuffer.h"


Framebuffer::Framebuffer( const char* name, int width, int height ): 
fboName(name), frameBuffer(0), colorFormat(0), depthBuffer(0), depthFormat(0), stencilBuffer(0), 
stencilFormat(0), width(width), height(height), colorTexnum(0), depthTexnum(0) {
	memset( colorBuffers, 0, sizeof( colorBuffers ) );

	glGenFramebuffers( 1, &frameBuffer );
}

void Framebuffer::PurgeFramebuffer() {
}

void Framebuffer::Bind() {
}

void Framebuffer::AddColorBuffer( GLuint format, int index ) {
}

void Framebuffer::AddDepthStencilBuffer( GLuint format ) {
}

int Framebuffer::Check() {
	return 0;
}
