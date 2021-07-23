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
#include <mutex>
#pragma hdrstop

#include "tr_local.h"

const int32 MAX_VERTCACHE_SIZE = INT_MAX;

idCVar idVertexCache::r_showVertexCache( "r_showVertexCache", "0", CVAR_INTEGER | CVAR_RENDERER, "Show VertexCache usage statistics" );
idCVar idVertexCache::r_frameVertexMemory( "r_frameVertexMemory", "4096", CVAR_INTEGER | CVAR_RENDERER | CVAR_ARCHIVE, "Initial amount of per-frame temporary vertex memory, in kB (max 131071)" );
idCVar idVertexCache::r_frameIndexMemory( "r_frameIndexMemory", "4096", CVAR_INTEGER | CVAR_RENDERER | CVAR_ARCHIVE, "Initial amount of per-frame temporary index memory, in kB (max 131071)" );
idCVar r_useFenceSync( "r_useFenceSync", "1", CVAR_BOOL | CVAR_RENDERER | CVAR_ARCHIVE, "Use GPU sync" );
idCVarBool r_usePersistentMapping( "r_usePersistentMapping", "1", CVAR_RENDERER | CVAR_ARCHIVE, "Use persistent buffer mapping" );

idVertexCache		vertexCache;
uint32_t			staticVertexSize, staticIndexSize;
attribBind_t		currentAttribBinding;

ALIGNTYPE16 const idDrawVert screenRectVerts[4] = {
	{idVec3( -1,-1,0 ),idVec2( 0,0 ), idVec3(), {idVec3(), idVec3()}, {255, 255, 255, 0}},
	{idVec3( +1,-1,0 ),idVec2( 1,0 ), idVec3(), {idVec3(), idVec3()}, {255, 255, 255, 0}},
	{idVec3( -1,+1,0 ),idVec2( 0,1 ), idVec3(), {idVec3(), idVec3()}, {255, 255, 255, 0}},
	{idVec3( +1,+1,0 ),idVec2( 1,1 ), idVec3(), {idVec3(), idVec3()}, {255, 255, 255, 0}},
};
ALIGNTYPE16 const glIndex_t screenRectIndices[6] = { 0, 2, 1, 1, 2, 3 };

static void ClearGeoBufferSet( geoBufferSet_t &gbs ) {
	gbs.mappedVertexBase = gbs.vertexBuffer.CurrentWriteLocation();
	gbs.mappedIndexBase = gbs.indexBuffer.CurrentWriteLocation();
	gbs.indexMemUsed.SetValue( 0 );
	gbs.vertexMemUsed.SetValue( 0 );
	gbs.allocations = 0;
}

static void SwitchFrameGeoBufferSet( geoBufferSet_t &gbs ) {
	int commitVertexBytes = Min( gbs.vertexMemUsed.GetValue(), (int)gbs.vertexBuffer.BytesRemaining() );
	gbs.vertexBuffer.Commit( commitVertexBytes );
	gbs.vertexBuffer.SwitchFrame();
	int commitIndexBytes = Min( gbs.indexMemUsed.GetValue(), (int)gbs.indexBuffer.BytesRemaining() );
	gbs.indexBuffer.Commit( commitIndexBytes );
	gbs.indexBuffer.SwitchFrame();
	ClearGeoBufferSet( gbs );
}

static void AllocGeoBufferSet( geoBufferSet_t &gbs, const int vertexBytes, const int indexBytes ) {
	gbs.vertexBuffer.InitWriteFrameAhead( GL_ARRAY_BUFFER, vertexBytes, VERTEX_CACHE_ALIGN );
	gbs.indexBuffer.InitWriteFrameAhead( GL_ELEMENT_ARRAY_BUFFER, indexBytes, INDEX_CACHE_ALIGN );
	GL_SetDebugLabel( GL_BUFFER, gbs.vertexBuffer.GetAPIObject(), "DynamicVertexCache" );
	GL_SetDebugLabel( GL_BUFFER, gbs.indexBuffer.GetAPIObject(), "DynamicIndexCache" );
	ClearGeoBufferSet( gbs );
}

static void FreeGeoBufferSet( geoBufferSet_t &gbs ) {
	gbs.vertexBuffer.Destroy();
	gbs.indexBuffer.Destroy();
}

/* duzenko 2.08
Vanilla D3 called VertexAttribPointer/Enable/DisableVertexAttribArray a few times for each draw
The whole purpose of moving to a single VBO was to be able to set base attrib pointer for many calls
Unrelated to that, we now also enable all attributes once and not bother driver with toggling them off and on all the time
*/
void BindAttributes( int pointer, attribBind_t attrib ) {
	if ( attrib == attribBind_t::ATTRIB_REGULAR ) {
		idDrawVert* ac = (idDrawVert*)(size_t)pointer;
		qglVertexAttribPointer( 0, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->xyz.ToFloatPtr() );
		qglVertexAttribPointer( 2, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->normal.ToFloatPtr() );
		qglVertexAttribPointer( 3, 4, GL_UNSIGNED_BYTE, true, sizeof( idDrawVert ), &ac->color );
		qglVertexAttribPointer( 8, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );
		qglVertexAttribPointer( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[0].ToFloatPtr() );
		qglVertexAttribPointer( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), ac->tangents[1].ToFloatPtr() );
	} else {
		const int attrib_indices[] = { 0,2,3,8,9,10 };
		for ( auto attr_index : attrib_indices )
			qglVertexAttribPointer( attr_index, 4, GL_FLOAT, false, sizeof( shadowCache_t ), (void*)(size_t)pointer );
	}
	currentAttribBinding = attrib;
}

/*
==============
idVertexCache::VertexPosition
==============
*/
void idVertexCache::VertexPosition( vertCacheHandle_t handle, attribBind_t attrib ) {
	GLuint vbo;
	if ( !handle.IsValid() ) {
		vbo = 0;
	} else {
		++vertexUseCount;
		vbo = handle.isStatic ? staticVertexBuffer : dynamicData.vertexBuffer.GetAPIObject();
	}
	if ( vbo != currentVertexBuffer ) {
		qglBindBuffer( GL_ARRAY_BUFFER, vbo );
		currentVertexBuffer = vbo;
		BindAttributes( 0, attrib );
	} else {
		// still need to switch to shadow attrib bindings when remain in the same VBO
		if ( currentAttribBinding != attrib )
			BindAttributes( 0, attrib );
	}
	if ( attrib == attribBind_t::ATTRIB_REGULAR ) 
		basePointer = handle.offset / sizeof( idDrawVert );
	else
		basePointer = handle.offset / sizeof( shadowCache_t );
}

/*
==============
idVertexCache::IndexPosition
==============
*/
void *idVertexCache::IndexPosition( vertCacheHandle_t handle ) {
	GLuint vbo;
	if ( !handle.IsValid() ) {
		vbo = 0;
	} else {
		++indexUseCount;
		vbo = handle.isStatic ? staticIndexBuffer : dynamicData.indexBuffer.GetAPIObject();
	}
	if ( vbo != currentIndexBuffer ) {
		qglBindBuffer( GL_ELEMENT_ARRAY_BUFFER, vbo );
		currentIndexBuffer = vbo;
	}
	return ( void * )( size_t )( handle.offset );
}

/*
==============
idVertexCache::UnbindIndex
==============
*/
void idVertexCache::UnbindIndex() {
	if ( currentIndexBuffer != 0 ) {
		qglBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
		currentIndexBuffer = 0;
	}
}

void idVertexCache::UnbindVertex() {
	qglBindBuffer( GL_ARRAY_BUFFER, 0 );
	currentVertexBuffer = 0;
}

//================================================================================

geoBufferSet_t::geoBufferSet_t() {}

/*
===========
idVertexCache::Init
===========
*/
void idVertexCache::Init() {
	currentFrame = 0;
	currentIndexCacheSize = idMath::Imin( MAX_VERTCACHE_SIZE, r_frameIndexMemory.GetInteger() * 1024 );
	currentVertexCacheSize = idMath::Imin( MAX_VERTCACHE_SIZE, r_frameVertexMemory.GetInteger() * 1024 );
	if ( currentIndexCacheSize <= 0 || currentVertexCacheSize <= 0 ) {
		common->FatalError( "Dynamic vertex cache size is invalid. Please adjust r_frameIndexMemory and r_frameVertexMemory." );
	}

	// 2.08 core context https://stackoverflow.com/questions/13403807/glvertexattribpointer-raising-gl-invalid-operation
	GLuint vao;
	qglGenVertexArrays( 1, &vao );
	qglBindVertexArray( vao ); 

	staticVertexBuffer = 0;
	staticIndexBuffer = 0;
	
	AllocGeoBufferSet( dynamicData, currentVertexCacheSize, currentIndexCacheSize );
	EndFrame();
}

/*
===========
idVertexCache::PurgeAll

Used when toggling vertex programs on or off, because
the cached data isn't valid
===========
*/
void idVertexCache::PurgeAll() {
	Shutdown();
	Init();
}

/*
===========
idVertexCache::Shutdown
===========
*/
void idVertexCache::Shutdown() {
	ClearGeoBufferSet( dynamicData );
	FreeGeoBufferSet( dynamicData );

	if ( staticVertexBuffer != 0 ) {
		qglDeleteBuffers( 1, &staticVertexBuffer );
		staticVertexBuffer = 0;
	}
	if ( staticIndexBuffer != 0 ) {
		qglDeleteBuffers( 1, &staticIndexBuffer );
		staticIndexBuffer = 0;
	}
}

/*
===========
idVertexCache::EndFrame
===========
*/
void idVertexCache::EndFrame() {
	// display debug information
	if ( r_showVertexCache.GetBool() ) {
		common->Printf( "vertex: %d times totaling %d kB, index: %d times totaling %d kB\n", vertexAllocCount, dynamicData.vertexMemUsed.GetValue() / 1024, indexAllocCount, dynamicData.indexMemUsed.GetValue() / 1024 );
	}

	// check if we need to increase the buffer size
	if ( dynamicData.indexMemUsed.GetValue() > currentIndexCacheSize ) {
		while ( currentIndexCacheSize <= MAX_VERTCACHE_SIZE / VERTCACHE_NUM_FRAMES / 2 && currentIndexCacheSize < dynamicData.indexMemUsed.GetValue() ) {
			currentIndexCacheSize *= 2;
		}
	}
	if ( dynamicData.vertexMemUsed.GetValue() > currentVertexCacheSize ) {
		while ( currentVertexCacheSize < MAX_VERTCACHE_SIZE / VERTCACHE_NUM_FRAMES / 2 && currentVertexCacheSize < dynamicData.vertexMemUsed.GetValue() ) {
			currentVertexCacheSize *= 2;
		}
	}

	// switch dynamic buffer to next frame
	SwitchFrameGeoBufferSet( dynamicData );
	currentFrame++;
	indexAllocCount = indexUseCount = vertexAllocCount = vertexUseCount = 0;

	// check if we need to resize current buffer set
	if (
		(int)dynamicData.vertexBuffer.BytesRemaining() < currentVertexCacheSize ||
		(int)dynamicData.indexBuffer.BytesRemaining() < currentIndexCacheSize
	) {
		common->Printf( "Resizing dynamic VertexCache: index %d kb -> %d kb, vertex %d kb -> %d kb\n", dynamicData.indexBuffer.BytesRemaining() / 1024, currentIndexCacheSize / 1024, dynamicData.indexBuffer.BytesRemaining() / 1024, currentVertexCacheSize / 1024 );
		FreeGeoBufferSet( dynamicData );
		AllocGeoBufferSet( dynamicData, currentVertexCacheSize, currentIndexCacheSize );
	}

	qglBindBuffer( GL_ARRAY_BUFFER, currentVertexBuffer = 0 );
	qglBindBuffer( GL_ELEMENT_ARRAY_BUFFER, currentIndexBuffer = 0 );

	// 2.08 temp helper for RB_DrawFullScreenQuad on core contexts
	screenRectSurf.ambientCache = AllocVertex( screenRectVerts, sizeof( screenRectVerts ) );
	screenRectSurf.indexCache = AllocIndex( &screenRectIndices, sizeof( screenRectIndices ) );
	screenRectSurf.numIndexes = 6;
}

/*
==============
idVertexCache::ActuallyAlloc
==============
*/
vertCacheHandle_t idVertexCache::ActuallyAlloc( geoBufferSet_t &vcs, const void *data, int bytes, cacheType_t type ) {
	if ( bytes == 0 ) {
		return NO_CACHE;
	}
	assert( ( ( ( uintptr_t )( data ) ) & 15 ) == 0 );
	const int alignSize = ( type == CACHE_INDEX ) ? INDEX_CACHE_ALIGN : VERTEX_CACHE_ALIGN;
	const int alignedBytes = ALIGN( bytes, alignSize );
	assert( ( alignedBytes & 15 ) == 0 );

	// thread safe interlocked adds
	byte **base = NULL;
	int	endPos = 0;
	int mapOffset = 0;

	if ( type == CACHE_INDEX ) {
		base = &vcs.mappedIndexBase;
		endPos = vcs.indexMemUsed.Add( alignedBytes );
		mapOffset = (intptr_t)vcs.indexBuffer.BufferOffset( vcs.mappedIndexBase );
		if ( endPos > currentIndexCacheSize ) {
			// out of index cache, will be resized next frame
			return NO_CACHE;
		}
		indexAllocCount++;
	} else if ( type == CACHE_VERTEX ) {
		base = &vcs.mappedVertexBase;
		endPos = vcs.vertexMemUsed.Add( alignedBytes );
		mapOffset = (intptr_t)vcs.vertexBuffer.BufferOffset( vcs.mappedVertexBase );
		if ( endPos > currentVertexCacheSize ) {
			// out of vertex cache, will be resized next frame
			return NO_CACHE;
		}
		vertexAllocCount++;
	} else {
		assert( false );
		return NO_CACHE;
	}
	vcs.allocations++;

	int offset = endPos - alignedBytes;

	// Actually perform the data transfer
	if ( data != NULL ) {
		void* dst = *base + offset;
		const byte* src = (byte*)data;
		assert_16_byte_aligned( dst );
		assert_16_byte_aligned( src );
		SIMDProcessor->Memcpy( dst, src, bytes );
	}

	return vertCacheHandle_t::Create(
		bytes,
		(mapOffset + offset),
		currentFrame & VERTCACHE_FRAME_MASK,
		false //&vcs == &staticData
	);
}

typedef idList <std::pair<const void*, int>> StaticList;
StaticList staticVertexList, staticIndexList;

/*
==============
idVertexCache::PrepareStaticCacheForUpload
==============
*/
void idVertexCache::PrepareStaticCacheForUpload() {
	// upload function to be called twice for vertex and index data
	auto upload = [](GLuint buffer, GLenum bufferType, int size, StaticList &staticList ) {
		int offset = 0;
		byte* ptr = (byte*)Mem_Alloc16( size );
		for ( auto& pair : staticList ) {
			memcpy( ptr + offset, pair.first, pair.second );
			offset += pair.second;
		}
		qglBindBuffer( bufferType, buffer );
		if ( GLAD_GL_ARB_buffer_storage ) {
			qglBufferStorage( bufferType, size, ptr, 0 );
		} else {
			qglBufferData( bufferType, size, ptr, GL_STATIC_DRAW );
		}
		Mem_Free16( ptr );
		staticList.ClearFree();
	};

	if ( staticVertexBuffer != 0 ) {
		qglDeleteBuffers( 1, &staticVertexBuffer );
	}
	if ( staticIndexBuffer != 0 ) {
		qglDeleteBuffers( 1, &staticIndexBuffer );
	}
	qglGenBuffers( 1, &staticVertexBuffer );
	qglGenBuffers( 1, &staticIndexBuffer );

	staticVertexSize = ALIGN( staticVertexSize, VERTEX_CACHE_ALIGN );
	upload( staticVertexBuffer, GL_ARRAY_BUFFER, staticVertexSize, staticVertexList );
	staticIndexSize = ALIGN( staticIndexSize, INDEX_CACHE_ALIGN );
	upload( staticIndexBuffer, GL_ELEMENT_ARRAY_BUFFER, staticIndexSize, staticIndexList );

	GL_SetDebugLabel( GL_BUFFER, staticVertexBuffer, "StaticVertexCache" );
	GL_SetDebugLabel( GL_BUFFER, staticIndexBuffer, "StaticIndexCache" );

	qglBindBuffer( GL_ARRAY_BUFFER, currentVertexBuffer = 0 );
	qglBindBuffer( GL_ELEMENT_ARRAY_BUFFER, currentIndexBuffer = 0 );
}

vertCacheHandle_t idVertexCache::AllocStaticVertex( const void* data, int bytes ) {
	if ( !staticVertexList.Num() )
		staticVertexSize = 0;
	staticVertexList.Append( std::make_pair( data, bytes ) );
	staticVertexSize += bytes;
	return vertCacheHandle_t::Create( bytes, staticVertexSize - bytes, 0, true );
}

vertCacheHandle_t idVertexCache::AllocStaticIndex( const void* data, int bytes ) {
	if ( !staticIndexList.Num() )
		staticIndexSize = 0;
	staticIndexList.Append( std::make_pair( data, bytes ) );
	staticIndexSize += bytes;
	return vertCacheHandle_t::Create( bytes, staticIndexSize - bytes, 0, true );
}
