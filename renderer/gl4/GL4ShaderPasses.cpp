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
#include "GLDebugGroup.h"
#include "OpenGL4Renderer.h"
#include "../FrameBuffer.h"

int GL4_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs, bool afterFog ) {

	GL_DEBUG_GROUP( DrawShaderPasses_STD, SHADER_PASS );
	// only obey skipAmbient if we are rendering a view
	if( backEnd.viewDef->viewEntitys && r_skipAmbient.GetInteger() == 1 )
		return numDrawSurfs;

	RB_LogComment( "---------- RB_STD_DrawShaderPasses ----------\n" );

	// if we are about to draw the first surface that needs
	// the rendering in a texture, copy it over
	if( drawSurfs[0]->material->GetSort() >= SS_AFTER_FOG && !backEnd.viewDef->IsLightGem() ) {
		if( r_skipPostProcess.GetBool() )
			return 0;

		// only dump if in a 3d view
		if( backEnd.viewDef->viewEntitys )
			FB_CopyColorBuffer();
		backEnd.currentRenderCopied = true;
	}

	GL_SelectTexture( 1 );
	globalImages->BindNull();

	GL_SelectTexture( 0 );

	backEnd.currentSpace = nullptr;
	int	i;
	for( i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t * surf = drawSurfs[i];
		const idMaterial * shader = surf->material;

		if( !shader->HasAmbient() ) {
			continue;
		}

		if( shader->IsPortalSky() ) {
			continue;
		}

		if( surf->material->GetSort() == SS_PORTAL_SKY && g_enablePortalSky.GetInteger() == 2 )
			continue;

		// some deforms may disable themselves by setting numIndexes = 0
		if( surf->backendGeo->numIndexes == 0 ) {
			continue;
		}

		if( shader->SuppressInSubview() ) {
			continue;
		}

		if( backEnd.viewDef->isXraySubview && surf->space->entityDef ) {
			if( surf->space->entityDef->parms.xrayIndex != 2 ) {
				continue;
			}
		}
		if( drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}

		if( backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef ) {
			if( drawSurfs[i]->space->entityDef->parms.xrayIndex != 2 ) {
				continue;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS && !backEnd.currentRenderCopied )
			break;

		if( drawSurfs[i]->material->GetSort() == SS_AFTER_FOG && !afterFog )
			break;

		//RB_STD_T_RenderShaderPasses( drawSurfs[i] );
	}

	GL_Cull( CT_FRONT_SIDED );

	return i;
}
