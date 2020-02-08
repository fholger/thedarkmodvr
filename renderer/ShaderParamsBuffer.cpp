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
#pragma hdrstop

#include "ShaderParamsBuffer.h"
#include "tr_local.h"

extern idCVarBool r_usePersistentMapping;

const uint32_t SHADER_BUFFER_SIZE = 8192 * 1024 * 3;

ShaderParamsBuffer::ShaderParamsBuffer() : mBufferObject( 0 ), mMapBase( nullptr ) {}

void ShaderParamsBuffer::Init() {
	if( mMapBase ) {
		Destroy();
	}

	GLint uboAlignment;
	qglGetIntegerv( GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uboAlignment );

	mSize = ALIGN( SHADER_BUFFER_SIZE, uboAlignment );
	mAlign = uboAlignment;
	qglGenBuffers( 1, &mBufferObject );
	qglBindBuffer( GL_UNIFORM_BUFFER, mBufferObject );

	mUsePersistentMapping = r_usePersistentMapping && glConfig.bufferStorageAvailable;

	if (mUsePersistentMapping) {
		qglBufferStorage( GL_UNIFORM_BUFFER, mSize, nullptr, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT );
		mMapBase = ( byte* )qglMapBufferRange( GL_UNIFORM_BUFFER, 0, mSize, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT );
	} else {
		qglBufferData( GL_UNIFORM_BUFFER, mSize, nullptr, GL_DYNAMIC_DRAW );
		mMapBase = ( byte* )Mem_Alloc16( mSize );
	}
	mCurrentOffset = 0;
	mLastLocked = 0;
}

void ShaderParamsBuffer::Destroy() {
	if( !mBufferObject ) {
		return;
	}

	for( auto it : mRangeLocks ) {
		qglDeleteSync( it.fenceSync );
	}
	mRangeLocks.clear();

	qglBindBuffer( GL_UNIFORM_BUFFER, mBufferObject );
	qglUnmapBuffer( GL_UNIFORM_BUFFER );
	qglDeleteBuffers( 1, &mBufferObject );
	mBufferObject = 0;
	if (!mUsePersistentMapping) {
		Mem_Free16( mMapBase );
	}
	mMapBase = nullptr;
}

byte *ShaderParamsBuffer::ReserveRaw( GLuint size ) {
	GLuint requestedSize = ALIGN( size, mAlign );
	if( requestedSize > mSize ) {
		common->Error( "Requested shader param size exceeds buffer size" );
	}

	if( mCurrentOffset + requestedSize > mSize ) {
		if( mCurrentOffset != mLastLocked ) {
			LockRange( mLastLocked, mCurrentOffset - mLastLocked );
		}
		mCurrentOffset = 0;
		mLastLocked = 0;
	}

	WaitForLockedRange( mCurrentOffset, requestedSize );
	byte *reserved = mMapBase + mCurrentOffset;
	mCurrentOffset += requestedSize;
}

void ShaderParamsBuffer::CommitRaw( byte *offset, GLuint size ) {
	GLuint requestedSize = ALIGN( size, mAlign );
	GLintptr mapOffset = offset - mMapBase;
	assert( mapOffset >= 0 && mapOffset < mSize );

	// for persistent mapping, nothing to do. Otherwise, we need to upload the committed data
	if( !mUsePersistentMapping ) {
		qglBindBuffer( GL_UNIFORM_BUFFER, mBufferObject );
		qglBufferSubData( GL_UNIFORM_BUFFER, mapOffset, requestedSize, offset );
	}
}

void ShaderParamsBuffer::BindRangeRaw( GLuint index, byte *offset, GLuint size ) {
	GLintptr mapOffset = offset - mMapBase;
	assert(mapOffset >= 0 && mapOffset < mSize);
	qglBindBufferRange( GL_UNIFORM_BUFFER, index, mBufferObject, mapOffset, size );
}

void ShaderParamsBuffer::Lock() {
	LockRange( mLastLocked, mCurrentOffset - mLastLocked );
	mLastLocked = mCurrentOffset;
}

void ShaderParamsBuffer::LockRange(GLuint offset, GLuint count ) {
	if( !mUsePersistentMapping ) {
		// don't need to lock without persistent mapping
		return;
	}

	GLsync fence = qglFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
	LockedRange range = { offset, count, fence };
	mRangeLocks.push_back( range );
}

void ShaderParamsBuffer::WaitForLockedRange(GLuint offset, GLuint count ) {
	LockedRange waitRange = { offset, count, 0 };
	for( auto it = mRangeLocks.begin(); it != mRangeLocks.end(); ) {
		if( waitRange.Overlaps( *it ) ) {
			Wait( *it );
			it = mRangeLocks.erase( it );
		} else {
			++it;
		}
	}
}

void ShaderParamsBuffer::Wait(LockedRange &range ) {
	GLenum result = qglClientWaitSync( range.fenceSync, 0, 0 );
	while( result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED ) {
		result = qglClientWaitSync( range.fenceSync, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000 );
		if( result == GL_WAIT_FAILED ) {
			assert( !"glClientWaitSync failed" );
			break;
		}
	}
	qglDeleteSync( range.fenceSync );
}
