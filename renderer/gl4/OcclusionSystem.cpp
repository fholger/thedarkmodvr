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

OcclusionSystem occlusionSystem;

OcclusionSystem::OcclusionSystem() : initialized(false) {}

void OcclusionSystem::Init() {
	if( initialized )
		return;

	bboxBuffer.Init( GL_ARRAY_BUFFER, 65536 * sizeof(Occluder), sizeof(Occluder) );
	qglGenBuffersARB( 1, &visibilityBuffer );
	qglBindBufferARB( GL_SHADER_STORAGE_BUFFER, visibilityBuffer );
	qglBufferStorage( GL_SHADER_STORAGE_BUFFER, 65536 * sizeof( int ), nullptr, 0 );
	qglGenBuffersARB( 1, &visibilityCopyBuffer );
	qglBindBufferARB( GL_SHADER_STORAGE_BUFFER, visibilityCopyBuffer );
	qglBufferStorage( GL_SHADER_STORAGE_BUFFER, 65536 * sizeof( int ), nullptr, 0 );
	visibility = ( int* )Mem_Alloc16( sizeof( int ) * 65536 );

	initialized = true;
}

void OcclusionSystem::Shutdown() {
	if( !initialized )
		return;

	bboxBuffer.Destroy();
	qglDeleteBuffersARB( 1, &visibilityBuffer );
	qglDeleteBuffersARB( 1, &visibilityCopyBuffer );
	Mem_Free16( visibility );
}

Occluder * OcclusionSystem::ReserveOccluders( uint count ) {
	byte *alloc = bboxBuffer.Reserve( count * sizeof(Occluder) );
	if( alloc == nullptr ) {
		common->FatalError( "Failed to allocate %d occluders in vertex buffer", count );
	}
	return ( Occluder* )alloc;
}

void OcclusionSystem::PrepareVisibilityBuffer() {
	qglBindBufferARB( GL_SHADER_STORAGE_BUFFER, visibilityBuffer );
	qglClearBufferData( GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr );
	qglBindBufferBase( GL_SHADER_STORAGE_BUFFER, 0, visibilityBuffer );
}

void OcclusionSystem::BindOccluders() {
	bboxBuffer.BindBuffer();
	qglVertexAttribPointer( 13, 4, GL_FLOAT, GL_FALSE, sizeof( Occluder ), (byte*)bboxBuffer.GetOffset() + offsetof( Occluder, bboxMin ) );
	qglVertexAttribPointer( 14, 4, GL_FLOAT, GL_FALSE, sizeof( Occluder ), (byte*)bboxBuffer.GetOffset() + offsetof( Occluder, bboxMax ) );
	qglEnableVertexAttribArray( 13 );
	qglEnableVertexAttribArray( 14 );
}

void OcclusionSystem::Finish( uint count ) {
	qglDisableVertexAttribArray( 13 );
	qglDisableVertexAttribArray( 14 );
	bboxBuffer.MarkAsUsed( count * sizeof( Occluder ) );

	// read back results; TODO might want to do this later to avoid stalls
	qglBindBufferARB( GL_COPY_READ_BUFFER, visibilityBuffer );
	qglBindBufferARB( GL_COPY_WRITE_BUFFER, visibilityCopyBuffer );
	qglCopyBufferSubData( GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, sizeof( int ) * count );
	qglGetBufferSubDataARB( GL_COPY_WRITE_BUFFER, 0, sizeof( int ) * count, visibility );
	visibleEntities.clear();
}

void OcclusionSystem::SetEntityIdVisible( int entityId ) {
	visibleEntities.insert( entityId );
}

bool OcclusionSystem::IsEntityIdVisible( int entityId ) const {
	return visibleEntities.find( entityId ) != visibleEntities.end();
}
