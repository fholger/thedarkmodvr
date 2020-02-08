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
#pragma once

class ShaderParamsBuffer {
public:
	ShaderParamsBuffer();

	void Init();
	void Destroy();

	void Lock();

	template<typename T>
	T *Request( uint32_t count ) {
		static_assert( sizeof(T) % 16 == 0,
			"UBO structs must be 16-byte aligned, use padding if necessary. Be sure to obey the std140 layout rules." );
		
		T *array = static_cast<T*>( ReserveRaw( sizeof(T) * count ) );
		return array;
	}

	template<typename T>
	void Commit(T *array, uint32_t count) {
		CommitRaw( (byte*)array, sizeof(T) * count );		
	}

	template<typename T>
	void BindRange( GLuint index, T *array, uint32_t count ) {
		BindRangeRaw( index, (byte*)array, sizeof(T) * count );		
	}

private:
	struct LockedRange {
		GLuint offset;
		GLuint count;
		GLsync fenceSync;

		bool Overlaps(const LockedRange& other) const {
			return offset < other.offset + other.count && other.offset < offset + count;
		}
	};

	bool mUsePersistentMapping;

	GLuint mBufferObject;
	GLuint mSize;
	GLuint mAlign;
	byte * mMapBase;
	GLuint mCurrentOffset;
	GLuint mLastLocked;
	std::vector<LockedRange> mRangeLocks;

	byte *ReserveRaw( GLuint size );
	void CommitRaw( byte *offset, GLuint size );
	void BindRangeRaw( GLuint index, byte *offset, GLuint size );
	void LockRange( GLuint offset, GLuint count );
	void WaitForLockedRange( GLuint offset, GLuint count );
	void Wait( LockedRange &range );	
};
