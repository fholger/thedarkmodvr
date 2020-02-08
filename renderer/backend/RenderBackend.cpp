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
#pragma hdrstop

#include "RenderBackend.h"
#include "../Profiling.h"

RenderBackend renderBackendImpl;
RenderBackend *renderBackend = &renderBackendImpl;

idCVar r_useNewBackend( "r_useNewBackend", "1", CVAR_BOOL|CVAR_RENDERER|CVAR_ARCHIVE, "Use experimental new backend" );

RenderBackend::RenderBackend() 
	: interactionStage( &shaderParamsBuffer )
{}

void RenderBackend::Init() {
	shaderParamsBuffer.Init();
	interactionStage.Init();
}

void RenderBackend::Shutdown() {
	shaderParamsBuffer.Destroy();
	interactionStage.Shutdown();
}

void RenderBackend::DrawView( const viewDef_t *viewDef ) {
	// we will need to do a new copyTexSubImage of the screen when a SS_POST_PROCESS material is used
	backEnd.currentRenderCopied = false;
	backEnd.afterFogRendered = false;

	GL_PROFILE( "DrawView" );

	// skip render bypasses everything that has models, assuming
	// them to be 3D views, but leaves 2D rendering visible
	if ( viewDef->viewEntitys && r_skipRender.GetBool() ) {
		return;
	}

	// skip render context sets the wgl context to NULL,
	// which should factor out the API cost, under the assumption
	// that all gl calls just return if the context isn't valid
	if ( viewDef->viewEntitys && r_skipRenderContext.GetBool() ) {
		GLimp_DeactivateContext();
	}
	backEnd.pc.c_surfaces += viewDef->numDrawSurfs;

	RB_ShowOverdraw();


	int processed;

	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

	// clear the z buffer, set the projection matrix, etc
	RB_BeginDrawingView();

	backEnd.lightScale = r_lightScale.GetFloat();
	backEnd.overBright = 1.0f;

	drawSurf_t **drawSurfs = ( drawSurf_t ** )&viewDef->drawSurfs[ 0 ];
	int numDrawSurfs = viewDef->numDrawSurfs;

	// if we are just doing 2D rendering, no need to fill the depth buffer
	if ( viewDef->viewEntitys ) {
		// fill the depth buffer and clear color buffer to black except on subviews
        void RB_STD_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs );
		RB_STD_FillDepthBuffer( drawSurfs, numDrawSurfs );
		RB_GLSL_DrawInteractions();
	}
		
	// now draw any non-light dependent shading passes
	int RB_STD_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs );
	processed = RB_STD_DrawShaderPasses( drawSurfs, numDrawSurfs );

	// fog and blend lights
	void RB_STD_FogAllLights( bool translucent );
	RB_STD_FogAllLights( false );

	// refresh fog and blend status 
	backEnd.afterFogRendered = true;

	// now draw any post-processing effects using _currentRender
	if ( processed < numDrawSurfs ) {
		RB_STD_DrawShaderPasses( drawSurfs + processed, numDrawSurfs - processed );
	}

	RB_STD_FogAllLights( true ); // 2.08: second fog pass, translucent only

	RB_RenderDebugTools( drawSurfs, numDrawSurfs );

	// restore the context for 2D drawing if we were stubbing it out
	if ( r_skipRenderContext.GetBool() && viewDef->viewEntitys ) {
		GLimp_ActivateContext();
		RB_SetDefaultGLState();
	}
}

void RenderBackend::EndFrame() {
	shaderParamsBuffer.Lock();
}
