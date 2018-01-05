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
#ifndef __GL_DEBUG_GROUP_H__
#define __GL_DEBUG_GROUP_H__

#include "../qgl.h"

enum DebugGroupId {
	RENDER,
	DEPTH,
	STENCIL,
	INTERACTION,
};

class GL_DebugGroup {
public:
	GL_DebugGroup(const char* message, GLuint id) {
		qglPushDebugGroup( GL_DEBUG_SOURCE_APPLICATION, id, -1, message );
	}

	~GL_DebugGroup() {
		qglPopDebugGroup();
	}
};

// place debug markers on entry and exit which can be used by OpenGL debuggers and profilers
#define GL_DEBUG_GROUP(msg, id) GL_DebugGroup glDebugGroup_##msg (#msg, id)

#endif