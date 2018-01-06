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

void RB_GLSL_CreateDrawInteractions( const drawSurf_t *surf );

struct StencilDrawData {
	float  modelMatrix[16];
	idVec4 lightOrigin;
};


void GL4_MultiDrawStencil( const drawSurf_t* drawSurfs, bool external ) {
	int count = 0;
	for( const drawSurf_t *surf = drawSurfs; surf; surf = surf->nextOnLight ) {
		++count;
	}
	int drawDataSize = count * sizeof( StencilDrawData );
	StencilDrawData* drawData = ( StencilDrawData* )openGL4Renderer.ReserveSSBO( drawDataSize );
	DrawElementsIndirectCommand* commands = openGL4Renderer.ReserveCommandBuffer( count );

	count = 0;
	for( const drawSurf_t *drawSurf = drawSurfs; drawSurf; drawSurf = drawSurf->nextOnLight ) {
		const srfTriangles_t *tri = drawSurf->backendGeo;
		if( vertexCache.CacheIsStatic( tri->shadowCache ) || vertexCache.CacheIsStatic( tri->indexCache ) )
			continue; // TODO: right now, shouldn't happen?

		bool isExt = false;
		int numIndexes;
		if( !r_useExternalShadows.GetInteger() ) {
			numIndexes = tri->numIndexes;
		}
		else if( r_useExternalShadows.GetInteger() == 2 ) { // force to no caps for testing
			numIndexes = tri->numShadowIndexesNoCaps;
		}
		else if( !( drawSurf->dsFlags & DSF_VIEW_INSIDE_SHADOW ) ) {
			// if we aren't inside the shadow projection, no caps are ever needed
			numIndexes = tri->numShadowIndexesNoCaps;
			isExt = true;
		}
		else if( !backEnd.vLight->viewInsideLight && !( drawSurf->backendGeo->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) ) {
			// if we are inside the shadow projection, but outside the light, and drawing
			// a non-infinite shadow, we can skip some caps
			if( backEnd.vLight->viewSeesShadowPlaneBits & drawSurf->backendGeo->shadowCapPlaneBits ) {
				// we can see through a rear cap, so we need to draw it, but we can skip the
				// caps on the actual surface
				numIndexes = tri->numShadowIndexesNoFrontCaps;
			}
			else {
				// we don't need to draw any caps
				numIndexes = tri->numShadowIndexesNoCaps;
			}
			isExt = true;
		}
		else {
			// must draw everything
			numIndexes = tri->numIndexes;
		}

		if( isExt != external )
			continue;

		memcpy( drawData[count].modelMatrix, drawSurf->space->modelMatrix, sizeof( drawData[0].modelMatrix ) );
		R_GlobalPointToLocal( drawSurf->space->modelMatrix, backEnd.vLight->globalLightOrigin, drawData[count].lightOrigin.ToVec3() );
		drawData[count].lightOrigin.w = 0.0f;

		commands[count].count = numIndexes;
		commands[count].instanceCount = 1;
		commands[count].firstIndex = ( ( tri->indexCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( glIndex_t );
		commands[count].baseVertex = ( ( tri->shadowCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( shadowCache_t );
		commands[count].baseInstance = count;
		++count;
	}
	if( count == 0 ) {
		return;
	}

	openGL4Renderer.BindSSBO( 0, drawDataSize );
	qglMultiDrawElementsIndirect( GL_TRIANGLES, GL_INDEX_TYPE, commands, count, 0 );
	openGL4Renderer.LockSSBO( drawDataSize );
}

void GL4_StencilShadowPass( const drawSurf_t* drawSurfs ) {
	GL_DEBUG_GROUP( GL4_StencilShadowPass, STENCIL );

	// TODO: only needed while mixing with legacy rendering
	openGL4Renderer.PrepareVertexAttribs();

	GL4Program stencilShader = openGL4Renderer.GetShader( SHADER_STENCIL_MD );
	stencilShader.Activate();

	globalImages->BindNull();
	GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS );
	if( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		qglPolygonOffset( r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat() );
		qglEnable( GL_POLYGON_OFFSET_FILL );
	}

	qglStencilFunc( GL_ALWAYS, 1, 255 );
	GL_Cull( CT_TWO_SIDED );

	if( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		qglEnable( GL_DEPTH_BOUNDS_TEST_EXT );
		qglDepthBoundsEXT( backEnd.vLight->scissorRect.zmin, backEnd.vLight->scissorRect.zmax );
	}

	float viewProjectionMatrix[16];
	myGlMultMatrix( backEnd.viewDef->worldSpace.modelViewMatrix, backEnd.viewDef->projectionMatrix, viewProjectionMatrix );
	stencilShader.SetUniformMatrix4( 0, viewProjectionMatrix );

	// currently shadows are all in frame temporary buffers
	openGL4Renderer.EnableVertexAttribs( { VA_SHADOWPOS, VA_DRAWID } );
	vertexCache.IndexPosition( 2 );

	// render non-external shadows
	qglStencilOpSeparate( backEnd.viewDef->isMirror ? GL_FRONT : GL_BACK, GL_KEEP, tr.stencilDecr, GL_KEEP );
	qglStencilOpSeparate( backEnd.viewDef->isMirror ? GL_BACK : GL_FRONT, GL_KEEP, tr.stencilIncr, GL_KEEP );
	GL4_MultiDrawStencil( drawSurfs, false );

	// render external shadows
	qglStencilOpSeparate( backEnd.viewDef->isMirror ? GL_FRONT : GL_BACK, GL_KEEP, GL_KEEP, tr.stencilIncr );
	qglStencilOpSeparate( backEnd.viewDef->isMirror ? GL_BACK : GL_FRONT, GL_KEEP, GL_KEEP, tr.stencilDecr );
	GL4_MultiDrawStencil( drawSurfs, true );

	// TODO: not necessary once full renderer is ported
	openGL4Renderer.EnableVertexAttribs( {VA_POSITION} );

	GL_CheckErrors();

	GL_Cull( CT_FRONT_SIDED );

	if( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() )
		qglDisable( GL_POLYGON_OFFSET_FILL );

	if( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() )
		qglDisable( GL_DEPTH_BOUNDS_TEST_EXT );

	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	if( !r_softShadowsQuality.GetBool() || backEnd.viewDef->renderView.viewID < TR_SCREEN_VIEW_ID )
		qglStencilFunc( GL_GEQUAL, 128, 255 );
}

void GL4_DrawLight_Stencil() {
	GL_DEBUG_GROUP( GL4_DrawLight_Stencil, INTERACTION );

	// clear the stencil buffer if needed
	if( backEnd.vLight->globalShadows || backEnd.vLight->localShadows ) {
		backEnd.currentScissor = backEnd.vLight->scissorRect;
		if( r_useScissor.GetBool() ) {
			qglScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}
		qglClear( GL_STENCIL_BUFFER_BIT );
	}
	else {
		// no shadows, so no need to read or write the stencil buffer
		qglStencilFunc( GL_ALWAYS, 128, 255 );
	}

	GL4_StencilShadowPass( backEnd.vLight->globalShadows );
	RB_GLSL_CreateDrawInteractions( backEnd.vLight->localInteractions );

	GL4_StencilShadowPass( backEnd.vLight->localShadows );
	RB_GLSL_CreateDrawInteractions( backEnd.vLight->globalInteractions );
}
