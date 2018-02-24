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
#ifndef __UNIFORM_BUFFER_OBJECT_H__
#define __UNIFORM_BUFFER_OBJECT_H__

#include "../qgl.h"

class UniformBufferObject {
public:
	UniformBufferObject() : buffer(0) {}
	~UniformBufferObject() { Destroy(); }

	void Init( GLsizeiptr size );
	void Destroy();

	void Bind( GLuint index );

	void Update( const void *data, GLsizeiptr size );

private:
	GLuint buffer;
};

#endif