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
#include "OpenGL4Renderer.h"
#include "GLDebugGroup.h"


idCVar r_useOpenGL4( "r_useOpenGL4", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "Use experimental OpenGL4 rendering backend" );

OpenGL4Renderer::OpenGL4Renderer()
{
}


OpenGL4Renderer::~OpenGL4Renderer()
{
}
