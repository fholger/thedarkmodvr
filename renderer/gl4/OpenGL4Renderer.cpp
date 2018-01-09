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
const int UBO_BUFFER_SIZE = 64 * 1024;

std::map<GLenum, GLuint> boundBuffers;
std::map<GLuint, GLuint> vertexAttribs;
std::set<GLuint> mappedBuffers;

PFNGLMAPBUFFERARBPROC originalMapBuffer;
PFNGLMAPBUFFERRANGEPROC originalMapBufferRange;
PFNGLBINDBUFFERARBPROC originalBindBuffer;
PFNGLUNMAPBUFFERARBPROC originalUnmapBuffer;
PFNGLBINDVERTEXBUFFERPROC originalBindVertexBuffer;

static void APIENTRY customBindBuffer(GLenum target, GLuint buffer)
{
	boundBuffers[target] = buffer;
	originalBindBuffer( target, buffer );
}

static void * APIENTRY customMapBuffer(GLenum target, GLenum access) {
	mappedBuffers.insert( boundBuffers[target] );
	return originalMapBuffer( target, access );
}

static void * APIENTRY customMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
	mappedBuffers.insert( boundBuffers[target] );
	return originalMapBufferRange( target, offset, length, access );
}

static GLboolean APIENTRY customUnmapBuffer(GLenum target) {
	mappedBuffers.erase( boundBuffers[target] );
	return originalUnmapBuffer( target );
}

static void APIENTRY customBindVertexBuffer( GLuint bindingindex, GLuint buffer, GLintptr offset, GLintptr stride ) {
	vertexAttribs[bindingindex] = buffer;
	originalBindVertexBuffer( bindingindex, buffer, offset, stride );
}

void APIENTRY DebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
	//common->Printf( "OpenGL message: %s\n", message );
	static idFile *file = nullptr;
	if( !file ) {
		idStr fileName;
		uint64_t currentTime = Sys_GetTimeMicroseconds();
		sprintf( fileName, "opengl_messages_%.20llu.txt", currentTime );
		file = fileSystem->OpenFileWrite( fileName, "fs_savepath", "" );
	}
	file->Printf( "CURRENT BOUND BUFFERS:\n" );
	for( auto pair : boundBuffers ) {
		file->Printf( "  %d - %d\n", pair.first, pair.second );
	}
	file->Printf( "CURRENT MAPPED BUFFERS: " );
	for( auto val : mappedBuffers ) {
		file->Printf( "%d ", val );
	}
	file->Printf( "\nCURRENT VERTEX ATTRIB BINDINGS: " );
	for( auto pair : vertexAttribs ) {
		file->Printf( "- %d: %d -", pair.first, pair.second );
	}
	file->Printf( "\n%s\n", message );
}


OpenGL4Renderer::OpenGL4Renderer() : initialized( false ), drawIdBuffer( 0 ), commandBuffer( nullptr ), enabledVertexAttribs( 0 ) {}


OpenGL4Renderer::~OpenGL4Renderer()
{
}


void OpenGL4Renderer::Init() {
	if( initialized )
		return;

	initialized = true;
	common->Printf( "Initializing OpenGL4 renderer backend ...\n" );

	LoadShaders();

	// generate draw id buffer, needed to pass a draw id to the vertex shader in multi-draw calls
	qglGenBuffersARB( 1, &drawIdBuffer );
	std::vector<uint32_t> drawIds( MAX_MULTIDRAW_COMMANDS );
	for( uint32_t i = 0; i < MAX_MULTIDRAW_COMMANDS; ++i ) {
		drawIds[i] = i;
	}
	qglBindBufferARB( GL_ARRAY_BUFFER, drawIdBuffer );
	qglBufferStorage( GL_ARRAY_BUFFER, drawIds.size() * sizeof( uint32_t ), drawIds.data(), 0 );

	ssbo.Init( GL_SHADER_STORAGE_BUFFER, MAX_MULTIDRAW_COMMANDS * MAX_ELEMENT_SIZE * BUFFER_FACTOR, glConfig.ssboOffsetAlignment );
	ubo.Init( UBO_BUFFER_SIZE );

	commandBuffer = ( DrawElementsIndirectCommand * )Mem_Alloc16( sizeof( DrawElementsIndirectCommand ) * MAX_MULTIDRAW_COMMANDS );

	common->Printf( "OpenGL4 renderer ready\n" );

#if OPENGL_DEBUG_CONTEXT == 1
	qglEnable( GL_DEBUG_OUTPUT );
	qglEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
	qglDebugMessageControl( GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_FALSE );
	qglDebugMessageControl( GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, GL_DONT_CARE, 0, 0, GL_TRUE );
	qglDebugMessageControl( GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DEBUG_TYPE_ERROR, GL_DONT_CARE, 0, 0, GL_TRUE );
	qglDebugMessageControl( GL_DEBUG_SOURCE_WINDOW_SYSTEM, GL_DEBUG_TYPE_ERROR, GL_DONT_CARE, 0, 0, GL_TRUE );
	qglDebugMessageControl( GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, GL_DONT_CARE, 0, 0, GL_TRUE );
	qglDebugMessageControl( GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, GL_DONT_CARE, 0, 0, GL_TRUE );
	qglDebugMessageControl( GL_DEBUG_SOURCE_WINDOW_SYSTEM, GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR, GL_DONT_CARE, 0, 0, GL_TRUE );
	qglDebugMessageCallback( DebugMessageCallback, 0 );
	/*originalBindBuffer = qglBindBufferARB;
	originalBindVertexBuffer = qglBindVertexBuffer;
	originalMapBuffer = qglMapBufferARB;
	originalUnmapBuffer = qglUnmapBuffer;
	originalMapBufferRange = qglMapBufferRange;
	qglBindBufferARB = customBindBuffer;
	qglBindVertexBuffer = customBindVertexBuffer;
	qglMapBufferRange = customMapBufferRange;
	qglMapBufferARB = customMapBuffer;
	qglUnmapBuffer = customUnmapBuffer;
	qglUnmapBufferARB = customUnmapBuffer;*/
#endif
}


void OpenGL4Renderer::Shutdown() {
	common->Printf( "Shutting down OpenGL4 renderer backend.\n" );

#if OPENGL_DEBUG_CONTEXT == 1
	qglDisable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
	qglDisable( GL_DEBUG_OUTPUT );
#endif

	DestroyShaders();

	qglDeleteBuffersARB( 1, &drawIdBuffer );
	ssbo.Destroy();
	ubo.Destroy();

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

void OpenGL4Renderer::BindSSBO( GLuint index, GLuint size ) {
	ssbo.BindBufferRange( index, size );
}

void OpenGL4Renderer::BindUBO( GLuint index ) {
	ubo.Bind( index );
}

void OpenGL4Renderer::UpdateUBO( const void *data, GLsizeiptr size ) {
	ubo.Update( data, size );
}

void OpenGL4Renderer::PrepareVertexAttribs() {
	qglVertexAttribFormat( VA_POSITION, 3, GL_FLOAT, GL_FALSE, offsetof( idDrawVert, xyz ) );
	qglVertexAttribFormat( VA_TEXCOORD, 2, GL_FLOAT, GL_FALSE, offsetof( idDrawVert, st ) );
	qglVertexAttribFormat( VA_NORMAL, 3, GL_FLOAT, GL_FALSE, offsetof( idDrawVert, normal ) );
	qglVertexAttribFormat( VA_TANGENT, 3, GL_FLOAT, GL_FALSE, offsetof( idDrawVert, tangents[0] ) );
	qglVertexAttribFormat( VA_BITANGENT, 3, GL_FLOAT, GL_FALSE, offsetof( idDrawVert, tangents[1] ) );
	qglVertexAttribFormat( VA_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof( idDrawVert, color ) );
	// used in stencil rendering
	qglVertexAttribFormat( VA_SHADOWPOS, 4, GL_FLOAT, GL_FALSE, offsetof( shadowCache_t, xyz ) );
	// used for multidraw commands
	qglBindBufferARB( GL_ARRAY_BUFFER, drawIdBuffer );
	qglVertexAttribIPointer( VA_DRAWID, 1, GL_UNSIGNED_INT, sizeof( uint32_t ), 0 );
	qglVertexAttribDivisor( VA_DRAWID, 1 );  // one element per instance, not per vertex

	// bind static and frame buffers to binding points
	qglBindVertexBuffer( 0, vertexCache.StaticVertexBuffer(), 0, sizeof( idDrawVert ) );
	qglBindVertexBuffer( 1, vertexCache.FrameVertexBuffer(), 0, sizeof( idDrawVert ) );
	// stencil shadow caches have a different stride
	qglBindVertexBuffer( 2, vertexCache.FrameVertexBuffer(), 0, sizeof( shadowCache_t ) );
	// buffer for draw ids
	//qglBindVertexBuffer( 3, drawIdBuffer, 0, sizeof( uint32_t ) );

	// initially bind static vertex buffer for all attributes except shadow and drawid
	qglVertexAttribBinding( VA_SHADOWPOS, 2 );
	//qglVertexAttribBinding( VA_DRAWID, 3 );
	boundStaticVertexBuffer = false;
	BindVertexBuffer( true );

	// initially disable all attributes
	enabledVertexAttribs = ~0;
	EnableVertexAttribs({});
}

void OpenGL4Renderer::EnableVertexAttribs( std::initializer_list<VertexAttribs> attribs ) {
	uint32_t enableAttribs = 0;
	for( auto attrib : attribs ) {
		enableAttribs |= ( 1 << attrib );
	}
	uint32_t diff = enabledVertexAttribs ^ enableAttribs;

	for( auto attrib : { VA_POSITION, VA_TEXCOORD, VA_NORMAL, VA_TANGENT, VA_BITANGENT, VA_COLOR, VA_SHADOWPOS, VA_DRAWID } ) {
		if( diff & ( 1 << attrib ) ) {
			if( enableAttribs & ( 1 << attrib ) ) {
				qglEnableVertexAttribArray( attrib );
			} else {
				qglDisableVertexAttribArray( attrib );
			}
		}
	}

	enabledVertexAttribs = enableAttribs;
}

void OpenGL4Renderer::BindVertexBuffer( bool staticBuffer ) {
	if( staticBuffer == boundStaticVertexBuffer )
		return;
	boundStaticVertexBuffer = staticBuffer;
	GLuint bufferIndex = staticBuffer ? 0 : 1;
	qglVertexAttribBinding( VA_POSITION, bufferIndex );
	qglVertexAttribBinding( VA_TEXCOORD, bufferIndex );
	qglVertexAttribBinding( VA_NORMAL, bufferIndex );
	qglVertexAttribBinding( VA_TANGENT, bufferIndex );
	qglVertexAttribBinding( VA_BITANGENT, bufferIndex );
	qglVertexAttribBinding( VA_COLOR, bufferIndex );
}

void OpenGL4Renderer::LoadShaders() {
	shaders[SHADER_DEPTH_FAST_MD] = GL4Program::Load( "depth_fast_md.vs", "black.fs" );
	shaders[SHADER_DEPTH_GENERIC] = GL4Program::Load( "depth_generic.vs", "depth_generic.fs" );
	shaders[SHADER_STENCIL_MD] = GL4Program::Load( "stencil_md.vs", "black.fs" );
	shaders[SHADER_INTERACTION_SIMPLE] = GL4Program::Load( "interaction_simple.vs", "interaction_simple.fs" );

	// validate all shaders
	for( int i = 0; i < TOTAL_SHADER_COUNT; ++i ) {
		if( !shaders[i].Valid() ) {
			common->Warning( "Not all shaders were successfully loaded, disabling GL4 renderer\n" );
			glConfig.openGL4Available = false;
			initialized = false;
			return;
		}
	}
}

void OpenGL4Renderer::DestroyShaders() {
	for( int i = 0; i < TOTAL_SHADER_COUNT; ++i ) {
		shaders[i].Destroy();
	}
}
