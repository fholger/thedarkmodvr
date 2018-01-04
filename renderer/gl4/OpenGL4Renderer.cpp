/*****************************************************************************
The Dark Mod GPL Source Code

This file is part of the The Dark Mod Source Code, originally based
on the Doom 3 GPL Source Code as published in 2011.

The Dark Mod Source Code is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version. For details, see LICENSE.TXT.

Project: The Dark Mod (http://www.thedarkmod.com/)

******************************************************************************/
#include "precompiled.h"
#include "OpenGL4Renderer.h"
#include "GLDebugGroup.h"
#include "../tr_local.h"

OpenGL4Renderer openGL4Renderer;
idCVar r_useOpenGL4( "r_useOpenGL4", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "Use experimental OpenGL4 rendering backend" );

const int MAX_MULTIDRAW_COMMANDS = 8192;
const int MAX_ELEMENT_SIZE = 256;
const int BUFFER_FACTOR = 3;

void GL4_BindBuffer(GLenum target, GLuint buffer) {
	openGL4Renderer.BindBuffer( target, buffer );
}


OpenGL4Renderer::OpenGL4Renderer() : initialized( false ), drawIdBuffer( 0 )
{
}


OpenGL4Renderer::~OpenGL4Renderer()
{
}


void OpenGL4Renderer::Init() {
	if( initialized )
		return;

	common->Printf( "Initializing OpenGL4 renderer backend ..." );

	// generate draw id buffer, needed to pass a draw id to the vertex shader in multi-draw calls
	qglGenBuffersARB( 1, &drawIdBuffer );
	std::vector<uint32_t> drawIds( MAX_MULTIDRAW_COMMANDS );
	for( uint32_t i = 0; i < MAX_MULTIDRAW_COMMANDS; ++i ) {
		drawIds[i] = i;
	}
	BindBuffer( GL_ARRAY_BUFFER, drawIdBuffer );
	qglBufferStorage( GL_ARRAY_BUFFER, drawIds.size() * sizeof( uint32_t ), drawIds.data(), 0 );

	ssbo.Init( GL_SHADER_STORAGE_BUFFER, MAX_MULTIDRAW_COMMANDS * MAX_ELEMENT_SIZE * BUFFER_FACTOR, glConfig.ssboOffsetAlignment );

	initialized = true;
	common->Printf( " Done\n" );
}


void OpenGL4Renderer::Shutdown() {
	common->Printf( "Shutting down OpenGL4 renderer backend.\n" );

	qglDeleteBuffersARB( 1, &drawIdBuffer );
	ssbo.Destroy();

	boundBuffers.clear();

	drawIdBuffer = 0;
	initialized = false;
}


void OpenGL4Renderer::BindBuffer( GLenum target, GLuint buffer ) {
	if( boundBuffers[target] != buffer ) {
		qglBindBufferARB( target, buffer );
		boundBuffers[target] = buffer;
	}
}
