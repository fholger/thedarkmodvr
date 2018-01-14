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
#include "OcclusionSystem.h"
#include "OpenGL4Renderer.h"

OcclusionSystem occlusionSystem;

OcclusionSystem::OcclusionSystem() : initialized(false) {}

const int ENTITIES_PER_INT = 32;
const int MAX_ENTITIES = 65536;
const int BIT_BUFFER_SIZE = sizeof(int) * (MAX_ENTITIES / ENTITIES_PER_INT + 1);

void OcclusionSystem::Init() {
	if( initialized )
		return;

	bboxBuffer.Init( GL_ARRAY_BUFFER, MAX_ENTITIES * sizeof(Occluder), sizeof(Occluder) );
	qglGenBuffersARB( 1, &visibilityBuffer );
	qglBindBufferARB( GL_SHADER_STORAGE_BUFFER, visibilityBuffer );
	qglBufferStorage( GL_SHADER_STORAGE_BUFFER, MAX_ENTITIES * sizeof( uint32_t ), nullptr, 0 );
	qglGenBuffersARB( 1, &visibilityCompressBuffer );
	qglBindBufferARB( GL_SHADER_STORAGE_BUFFER, visibilityCompressBuffer );
	qglBufferStorage( GL_SHADER_STORAGE_BUFFER, BIT_BUFFER_SIZE, nullptr, 0 );
	qglGenBuffersARB( 1, &visibilityCopyBuffer );
	qglBindBufferARB( GL_SHADER_STORAGE_BUFFER, visibilityCopyBuffer );
	qglBufferStorage( GL_SHADER_STORAGE_BUFFER, BIT_BUFFER_SIZE, nullptr, 0 );
	visibilityResults = ( uint32_t* )Mem_Alloc16( BIT_BUFFER_SIZE );
	testedEntities = ( uint32_t* )Mem_Alloc16( BIT_BUFFER_SIZE );
	lastFrameCulled = ( uint32_t* )Mem_Alloc16( BIT_BUFFER_SIZE );

	initialized = true;
}

void OcclusionSystem::Shutdown() {
	if( !initialized )
		return;

	bboxBuffer.Destroy();
	qglDeleteBuffersARB( 1, &visibilityBuffer );
	qglDeleteBuffersARB( 1, &visibilityCopyBuffer );
	Mem_Free16( visibilityResults );
	Mem_Free16( testedEntities );
	Mem_Free16( lastFrameCulled );
}

Occluder * OcclusionSystem::ReserveOccluders( uint count ) {
	byte *alloc = bboxBuffer.Reserve( count * sizeof(Occluder) );
	if( alloc == nullptr ) {
		common->FatalError( "Failed to allocate %d occluders in vertex buffer", count );
	}
	return ( Occluder* )alloc;
}

void OcclusionSystem::BeginFrame() {
	Init();
	memset( testedEntities, 0, BIT_BUFFER_SIZE );
	memset( visibilityResults, 0, BIT_BUFFER_SIZE );
	qglBindBufferARB( GL_SHADER_STORAGE_BUFFER, visibilityBuffer );
	qglClearBufferData( GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr );
}

void OcclusionSystem::EndFrame() {
	memset( lastFrameCulled, 0, BIT_BUFFER_SIZE );
	for( int i = 0; i < MAX_ENTITIES / ENTITIES_PER_INT; ++i ) {
		lastFrameCulled[i] = testedEntities[i] & (~visibilityResults[i]);
	}

	int culledCount = 0;
	for( int i = 0; i < MAX_ENTITIES; ++i ) {
		int idx = i / ENTITIES_PER_INT;
		int bit = i % ENTITIES_PER_INT;
		if( lastFrameCulled[idx] & ( 1 << bit ) )
			++culledCount;
	}
	common->Printf( "Number of entities culled: %d\n", culledCount );
}

void OcclusionSystem::PrepareVisibilityBuffer() {
	qglBindBufferBase( GL_SHADER_STORAGE_BUFFER, 0, visibilityBuffer );
}

void OcclusionSystem::BindOccluders() {
	bboxBuffer.BindBuffer();
	qglVertexAttribPointer( 12, 3, GL_FLOAT, GL_FALSE, sizeof( Occluder ), (byte*)bboxBuffer.GetOffset() + offsetof( Occluder, bboxMin ) );
	qglVertexAttribPointer( 13, 3, GL_FLOAT, GL_FALSE, sizeof( Occluder ), (byte*)bboxBuffer.GetOffset() + offsetof( Occluder, bboxMax ) );
	qglVertexAttribIPointer( 14, 1, GL_UNSIGNED_INT, sizeof( Occluder ), ( byte* )bboxBuffer.GetOffset() + offsetof( Occluder, entityId ) );
	qglEnableVertexAttribArray( 12 );
	qglEnableVertexAttribArray( 13 );
	qglEnableVertexAttribArray( 14 );
}

void OcclusionSystem::CompressOutput() {
	qglBindBufferARB( GL_ARRAY_BUFFER, visibilityBuffer );
	openGL4Renderer.EnableVertexAttribs( {} );
	for( int i = 7; i < 15; i++ ) {
		qglVertexAttribIPointer( i, 4, GL_UNSIGNED_INT, sizeof( int ) * 32, ( const void* )( i*sizeof( int ) * 4 ) );
		qglVertexAttribDivisor( i, 0 );
		qglEnableVertexAttribArray( i );
	}
	qglBindBufferBase( GL_SHADER_STORAGE_BUFFER, 0, visibilityCompressBuffer );
	qglMemoryBarrier( GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT );

	openGL4Renderer.GetShader( SHADER_OCCLUSION_BITPACK ).Activate();
	qglEnable( GL_RASTERIZER_DISCARD );
	qglDrawArrays( GL_POINTS, 0, MAX_ENTITIES / ENTITIES_PER_INT );
	qglDisable( GL_RASTERIZER_DISCARD );
	qglMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT );

	for( int i = 7; i < 15; ++i ) {
		qglDisableVertexAttribArray( i );
	}
	openGL4Renderer.EnableVertexAttribs( { VA_POSITION } );
}

void OcclusionSystem::Finish( uint count ) {
	qglDisableVertexAttribArray( 12 );
	qglDisableVertexAttribArray( 13 );
	qglDisableVertexAttribArray( 14 );
	bboxBuffer.MarkAsUsed( count * sizeof( Occluder ) );
	CompressOutput();
}

void OcclusionSystem::TransferResults() {
	// read back results
	qglBindBufferARB( GL_COPY_READ_BUFFER, visibilityCompressBuffer );
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, visibilityCopyBuffer );
	qglCopyBufferSubData( GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, BIT_BUFFER_SIZE );
	qglGetBufferSubDataARB( GL_COPY_WRITE_BUFFER, 0, BIT_BUFFER_SIZE, visibilityResults );
}

bool OcclusionSystem::WasEntityCulledLastFrame( int entityId ) const {
	//return false;
	if( !initialized || entityId < 0 || entityId >= MAX_ENTITIES )
		return false;
	int arrayIndex = entityId / 32;
	int bitPosition = entityId % 32;
	return ( lastFrameCulled[arrayIndex] & ( 1 << bitPosition ) ) != 0;
}

void OcclusionSystem::SetEntityIdTested( int entityId ) {
	int arrayIndex = entityId / 32;
	int bitPosition = entityId % 32;
	testedEntities[arrayIndex] |= ( 1 << bitPosition );
}
