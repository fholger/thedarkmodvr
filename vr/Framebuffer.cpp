#include "precompiled.h"
#include "Framebuffer.h"
#include "../tools/radiant/NewTexWnd.h"
#include "../sys/win32/win_local.h"

Framebuffer* primaryFramebuffer = nullptr;

Framebuffer::Framebuffer( const char* name, int width, int height ): 
fboName(name), frameBuffer(0), colorFormat(0), depthBuffer(0), depthFormat(0), stencilBuffer(0), 
stencilFormat(0), width(width), height(height), colorTexnum(0), depthTexnum(0) {
	memset( colorBuffers, 0, sizeof( colorBuffers ) );

	qglGenFramebuffers( 1, &frameBuffer );
}

Framebuffer::~Framebuffer() {
	if (depthBuffer != 0) {
		qglDeleteRenderbuffers( 1, &depthBuffer );
	}
	for (int i = 0; i < 16; ++i) {
		if (colorBuffers[i] != 0) {
			qglDeleteRenderbuffers( 1, &colorBuffers[i] );
		}
	}
	qglDeleteFramebuffers( 1, &frameBuffer );
}

void Framebuffer::Bind() {
	if (backEnd.glState.currentFramebuffer != this) {
		qglBindFramebuffer( GL_FRAMEBUFFER, frameBuffer );
		backEnd.glState.currentFramebuffer = this;
		wglSwapIntervalEXT( 0 );
	}
}

void Framebuffer::BindPrimary() {
	qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	qglBindRenderbuffer( GL_RENDERBUFFER, 0 );
	backEnd.glState.currentFramebuffer = nullptr;

	if (primaryFramebuffer != nullptr) {
		primaryFramebuffer->Bind();
		glConfig.vidWidth = primaryFramebuffer->GetWidth();
		glConfig.vidHeight = primaryFramebuffer->GetHeight();
	} else {
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
		//glConfig.vidWidth = glConfig.windowWidth;
		//glConfig.vidHeight = glConfig.windowHeight;
	}
}

void Framebuffer::AddColorBuffer( GLuint format, int index ) {
	if (index < 0 || index >= 16) { // FIXME: retrieve actual upper value from OpenGL
		common->Warning( "Framebuffer::AddColorBuffer( %s ): bad index = %i", fboName.c_str(), index );
		return;		
	}
	GL_CheckErrors();
	colorFormat = format;

	bool notCreatedYet = colorBuffers[index] == 0;
	if (notCreatedYet) {
		qglGenRenderbuffers( 1, &colorBuffers[index] );
		GL_CheckErrors();
	}

	qglBindRenderbuffer( GL_RENDERBUFFER, colorBuffers[index] );
	GL_CheckErrors();
	qglRenderbufferStorage( GL_RENDERBUFFER, format, width, height );
	GL_CheckErrors();

	if (notCreatedYet) {
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_RENDERBUFFER, colorBuffers[index] );
		GL_CheckErrors();
	}
}

void Framebuffer::AddColorImage( idImage* colorImage, int index, int mipmapLod ) {
	if (index < 0 || index >= 16) { // FIXME: retrieve actual upper value from OpenGL
		common->Warning( "Framebuffer::AddColorBuffer( %s ): bad index = %i", fboName.c_str(), index );
		return;
	}

	qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, colorImage->texnum, mipmapLod );
}

void Framebuffer::AddDepthStencilBuffer( GLuint format ) {
	depthFormat = format;

	bool notCreatedYet = depthBuffer == 0;
	if (notCreatedYet) {
		qglGenRenderbuffers( 1, &depthBuffer );
	}

	qglBindRenderbuffer( GL_RENDERBUFFER, depthBuffer );
	qglRenderbufferStorage( GL_RENDERBUFFER, format, width, height );
	qglBindRenderbuffer( GL_RENDERBUFFER, 0 );

	if (notCreatedYet) {
		qglBindFramebuffer( GL_FRAMEBUFFER, frameBuffer );
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer );
	}

	notCreatedYet = stencilBuffer == 0;
	if (notCreatedYet) {
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer );
		stencilBuffer = depthBuffer;
		stencilFormat = depthFormat;
	}
}

void Framebuffer::Check() {
	int prev;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &prev );

	qglBindFramebuffer( GL_FRAMEBUFFER, frameBuffer );

	int status = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if (status == GL_FRAMEBUFFER_COMPLETE) {
		qglBindFramebuffer( GL_FRAMEBUFFER, prev );
		return;
	}

	// something went wrong
	switch (status) {
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		common->Error( "Framebuffer::Check( %s ): Framebuffer incomplete, incomplete attachment", fboName.c_str() );
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
		common->Error( "Framebuffer::Check( %s ): Framebuffer incomplete, missing attachment", fboName.c_str() );
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
		common->Error( "Framebuffer::Check( %s ): Framebuffer incomplete, missing draw buffer", fboName.c_str() );
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
		common->Error( "Framebuffer::Check( %s ): Framebuffer incomplete, missing read buffer", fboName.c_str() );
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
		common->Error( "Framebuffer::Check( %s ): Framebuffer incomplete, missing layer targets", fboName.c_str() );
		break;

	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
		common->Error( "Framebuffer::Check( %s ): Framebuffer incomplete, missing multisample", fboName.c_str() );
		break;

	case GL_FRAMEBUFFER_UNSUPPORTED:
		common->Error( "Framebuffer::Check( %s ): Unsupported framebuffer format", fboName.c_str() );
		break;

	default:
		common->Error( "Framebuffer::Check( %s ): Unknown error 0x%X", fboName.c_str(), status );
		break;
	};

	qglBindFramebuffer( GL_FRAMEBUFFER, prev );
}
