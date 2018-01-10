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
#include "GL4Backend.h"
#include "OpenGL4Renderer.h"
#include "GLDebugGroup.h"

idCVar r_useOpenGL4( "r_useOpenGL4", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "Use experimental OpenGL4 rendering backend" );

// TODO: remove when full renderer complete
#include "../glsl.h"
extern bool afterFog;
int RB_STD_DrawShaderPasses( drawSurf_t ** drawSurfs, int numDrawSurfs );
// ----------------------------------------

void GL4_DrawView( void ) {
	drawSurf_t	 **drawSurfs;
	int			numDrawSurfs, processed;

	RB_LogComment( "---------- RB_STD_DrawView ----------\n" );

	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

	drawSurfs = ( drawSurf_t ** )&backEnd.viewDef->drawSurfs[0];
	numDrawSurfs = backEnd.viewDef->numDrawSurfs;

	GL_DEBUG_GROUP( DrawView_GL4, RENDER );

	// clear the z buffer, set the projection matrix, etc
	RB_BeginDrawingView();

	backEnd.lightScale = r_lightScale.GetFloat();
	backEnd.overBright = 1.0f;

	// fill the depth buffer and clear color buffer to black except on subviews
	GL4_FillDepthBuffer( drawSurfs, numDrawSurfs );

	GL4_DrawInteractions();

	// now draw any non-light dependent shading passes
	processed = GL4_DrawShaderPasses( drawSurfs, numDrawSurfs, false );

	
	// fog and blend lights
	/*RB_STD_FogAllLights();
	afterFog = true;

	// now draw any post-processing effects using _currentRender
	if( processed < numDrawSurfs ) {
		GL4_DrawShaderPasses( drawSurfs + processed, numDrawSurfs - processed, true );
	}

	RB_RenderDebugTools( drawSurfs, numDrawSurfs );*/
	
}
