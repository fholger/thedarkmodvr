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
#ifndef __GL4_PROGRAM_H__
#define __GL4_PROGRAM_H__

#include "../qgl.h"
#include "../tr_local.h"

class GL4Program {
public:
	static GL4Program Load( const char* vertex, const char* fragment = nullptr, const char* geometry = nullptr );
	void Destroy();
	bool Valid() const { return program != 0; }

	void Activate() const;
	static void Unset();

	void SetUniform1( GLint location, GLfloat value );
	void SetUniform3( GLint location, const GLfloat *values );
	void SetUniform4( GLint location, const GLfloat *values );
	void SetUniformMatrix4( GLint location, const GLfloat *matrix );
	void SetProjectionMatrix( GLint location );
	void SetViewMatrix( GLint location );
	void SetViewProjectionMatrix( GLint location );
	void SetModelViewProjectionMatrix( GLint location, const viewEntity_t *entity );

	GL4Program();
private:
	GLuint program;

	explicit GL4Program( GLuint program );
};

#endif