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
#include "GL4Backend.h"
#include "../tr_local.h"

OpenGL4Renderer openGL4Renderer;

const int MAX_MULTIDRAW_COMMANDS = 8192;
const int MAX_ELEMENT_SIZE = 256;
const int BUFFER_FACTOR = 3;


OpenGL4Renderer::OpenGL4Renderer() : initialized( false ), drawIdBuffer( 0 ), commandBuffer( nullptr ) {}


OpenGL4Renderer::~OpenGL4Renderer()
{
}


void OpenGL4Renderer::Init() {
	if( initialized )
		return;

	common->Printf( "Initializing OpenGL4 renderer backend ...\n" );

	LoadShaders();

	// generate draw id buffer, needed to pass a draw id to the vertex shader in multi-draw calls
	qglGenBuffersARB( 1, &drawIdBuffer );
	std::vector<uint32_t> drawIds( MAX_MULTIDRAW_COMMANDS );
	for( uint32_t i = 0; i < MAX_MULTIDRAW_COMMANDS; ++i ) {
		drawIds[i] = i;
	}
	BindBuffer( GL_ARRAY_BUFFER, drawIdBuffer );
	qglBufferStorage( GL_ARRAY_BUFFER, drawIds.size() * sizeof( uint32_t ), drawIds.data(), 0 );

	ssbo.Init( GL_SHADER_STORAGE_BUFFER, MAX_MULTIDRAW_COMMANDS * MAX_ELEMENT_SIZE * BUFFER_FACTOR, glConfig.ssboOffsetAlignment );

	commandBuffer = ( DrawElementsIndirectCommand * )Mem_Alloc16( sizeof( DrawElementsIndirectCommand ) * MAX_MULTIDRAW_COMMANDS );

	initialized = true;
	common->Printf( "OpenGL4 renderer ready\n" );
}


void OpenGL4Renderer::Shutdown() {
	common->Printf( "Shutting down OpenGL4 renderer backend.\n" );

	DestroyShaders();

	qglDeleteBuffersARB( 1, &drawIdBuffer );
	ssbo.Destroy();

	Mem_Free16( commandBuffer );

	boundBuffers.clear();

	drawIdBuffer = 0;
	commandBuffer = nullptr;
	initialized = false;
}

DrawElementsIndirectCommand * OpenGL4Renderer::ReserveCommandBuffer( uint count ) {
	if( count > MAX_MULTIDRAW_COMMANDS ) {
		common->FatalError( "Requested count %d exceeded maximum command buffer size for multi-draw calls", count );
	}

	// currently just return local memory; might want to change this to a persistently mapped GPU buffer?
	return commandBuffer;
}

byte * OpenGL4Renderer::ReserveSSBO( uint size ) {
	byte *alloc = ssbo.Reserve( size );
	if( alloc == nullptr ) {
		common->FatalError( "Failed to allocate %d bytes of SSBO storage", size );
	}
	return alloc;
}

void OpenGL4Renderer::LockSSBO( uint size ) {
	ssbo.MarkAsUsed( size );
}

GL4Program OpenGL4Renderer::GetShader( ProgramType shaderType ) const {
	return shaders[shaderType];
}

void OpenGL4Renderer::BindDrawId( GLuint index ) {
	BindBuffer( GL_ARRAY_BUFFER, drawIdBuffer );
	qglVertexAttribIPointer( index, 1, GL_UNSIGNED_INT, sizeof( uint32_t), 0 );
	qglVertexAttribDivisor( index, 1 );
	qglEnableVertexAttribArray( index );
}

void OpenGL4Renderer::BindSSBO( GLuint index, GLuint size ) {
	ssbo.BindBufferRange( index, size );
}


void OpenGL4Renderer::BindBuffer( GLenum target, GLuint buffer ) {
	/*if( boundBuffers[target] != buffer )*/ {
		qglBindBufferARB( target, buffer );
		boundBuffers[target] = buffer;
	}
}

void OpenGL4Renderer::LoadShaders() {
	shaders[SHADER_DEPTH_FAST_MD] = GL4Program::Load( "depth_fast_md.vs", "black.fs" );

	// validate all shaders
	for( int i = 0; i < TOTAL_SHADER_COUNT; ++i ) {
		if( !shaders[i].Valid() ) {
			common->Warning( "Not all shaders were successfully loaded, disabling GL4 renderer\n" );
			glConfig.openGL4Available = false;
			return;
		}
	}
}

void OpenGL4Renderer::DestroyShaders() {
	for( int i = 0; i < TOTAL_SHADER_COUNT; ++i ) {
		shaders[i].Destroy();
	}
}
