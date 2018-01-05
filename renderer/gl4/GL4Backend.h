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
#ifndef __GL4_BACKEND_H__
#define __GL4_BACKEND_H__

#include "../qgl.h"
#include "../tr_local.h"

extern idCVar r_useOpenGL4;

struct DrawElementsIndirectCommand {
	uint count;
	uint instanceCount;
	uint firstIndex;
	uint baseVertex;
	uint baseInstance;
};

void GL4_BindBuffer( GLenum target, GLuint buffer );

void GL4_DrawView();
void GL4_FillDepthBuffer( drawSurf_t ** drawSurfs, int numDrawSurfs );

#endif