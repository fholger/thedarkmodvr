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
#include "UniformBufferObject.h"

void UniformBufferObject::Init( GLsizeiptr size ) {
	if( buffer != 0 ) {
		Destroy();
	}
	qglGenBuffersARB( 1, &buffer );
	qglBindBufferARB( GL_UNIFORM_BUFFER, buffer );
	qglBufferStorage( GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_STORAGE_BIT );
}

void UniformBufferObject::Destroy() {
	if( buffer != 0 ) {
		qglDeleteBuffersARB( 1, &buffer );
		buffer = 0;
	}
}

void UniformBufferObject::Bind( GLuint index ) {
	qglBindBufferBase( GL_UNIFORM_BUFFER, index, buffer );
}

void UniformBufferObject::Update( const void *data, GLsizeiptr size ) {
	qglBindBufferARB( GL_UNIFORM_BUFFER, buffer );
	qglBufferSubData( GL_UNIFORM_BUFFER, 0, size, data );
}
