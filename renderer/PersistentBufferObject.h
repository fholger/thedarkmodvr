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

#ifndef __PERSISTENT_BUFFER_OBJECT_H__
#define __PERSISTENT_BUFFER_OBJECT_H__

#include "qgl.h"

struct LockedRange {
	GLuint offset;
	GLuint count;
	GLsync fenceSync;

	bool Overlaps(const LockedRange& other) const {
		return offset < other.offset + other.count && other.offset < offset + count;
	}
};

template<class T>
class PersistentBufferObject
{
public:
	PersistentBufferObject() : mBufferObject(0), mSize(0), mMapBase(nullptr), mCurrentOffset(0) {}
	
	~PersistentBufferObject() {
		Destroy();
	}

	void Init(GLenum target, GLuint size) {
		if( mMapBase ) {
			Destroy();
		}

		mTarget = target;
		mSize = size;
		qglGenBuffersARB( 1, &mBufferObject );
		qglBindBufferARB( mTarget, mBufferObject );
		qglBufferStorage( mTarget, sizeof( T ) * size, nullptr, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT );
		mMapBase = reinterpret_cast< T* >( qglMapBufferRange( mTarget, 0, sizeof( T ) * size, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT ) );
		mCurrentOffset = 0;
	}

	void Destroy() {
		if( !mBufferObject ) {
			return;
		}

		qglBindBufferARB( mTarget, mBufferObject );
		qglUnmapBuffer( mTarget );
		qglDeleteBuffersARB( 1, &mBufferObject );
		mBufferObject = 0;
		mMapBase = nullptr;
	}

	T* Reserve(GLuint count) {
		if( count > mSize ) {
			return nullptr;
		}

		if( mCurrentOffset + count > mSize ) {
			mCurrentOffset = 0;
		}

		WaitForLockedRange( mCurrentOffset, count );
		return mMapBase + mCurrentOffset;
	}

	void MarkAsUsed(GLuint count) {
		LockRange( mCurrentOffset, count );
		//mCurrentOffset += count;
	}

	void BindBuffer() {
		qglBindBufferARB( mTarget, mBufferObject );
	}

	void BindBufferRange(GLuint index, GLuint count) {
		qglBindBufferRange( mTarget, index, mBufferObject, mCurrentOffset * sizeof( T ), count * sizeof( T ) );
	}

	void BindBufferBase(GLuint index) {
		qglBindBufferBase( mTarget, index, mBufferObject );
	}

	void* GetOffset() const { return ( void* )( mCurrentOffset * sizeof( T ) ); }

private:
	GLuint mBufferObject;
	GLenum mTarget;
	GLuint mSize;
	T * mMapBase;
	GLuint mCurrentOffset;
	std::vector<LockedRange> mRangeLocks;

	void LockRange(GLuint offset, GLuint count) {
		GLsync fence = qglFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
		LockedRange range = { offset, count, fence };
		mRangeLocks.push_back( range );
	}

	void WaitForLockedRange(GLuint offset, GLuint count) {
		LockedRange waitRange = { offset, count, 0 };
		for( auto it = mRangeLocks.begin(); it != mRangeLocks.end(); ) {
			if( waitRange.Overlaps(*it) ) {
				Wait( *it );
				it = mRangeLocks.erase( it );
			} else {
				++it;
			}
		}
	}

	void Wait(LockedRange& range) {
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
};

#endif
