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
#include "../tr_local.h"
#include "GL4Backend.h"
#include "GLDebugGroup.h"
#include "OpenGL4Renderer.h"

struct DepthDrawData {
	float modelViewMatrix[16];
};

const int SU_LOC_PROJ_MATRIX = 0;

// TODO: remove when full renderer complete
void RB_STD_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs );
// ----------------------------------------

void GL4_MultiDrawDepth( drawSurf_t **drawSurfs, int numDrawSurfs, bool staticVertex, bool staticIndex ) {
	if( numDrawSurfs == 0 ) {
		return;
	}

	DrawElementsIndirectCommand *commands = openGL4Renderer.ReserveCommandBuffer( numDrawSurfs );
	GLuint ssboSize = sizeof( DepthDrawData ) * numDrawSurfs;
	DepthDrawData *drawData = ( DepthDrawData* )openGL4Renderer.ReserveSSBO( ssboSize );

	for( int i = 0; i < numDrawSurfs; ++i ) {
		memcpy( drawData[i].modelViewMatrix, drawSurfs[i]->space->modelViewMatrix, sizeof( drawData[0].modelViewMatrix ) );
		const srfTriangles_t *tri = drawSurfs[i]->backendGeo;
		commands[i].count = tri->numIndexes;
		commands[i].instanceCount = 1;
		if( !tri->indexCache ) {
			common->Warning( "GL4_MultiDrawDepth: Missing indexCache" );
		}
		commands[i].firstIndex = ( ( tri->indexCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( glIndex_t );
		commands[i].baseVertex = ( ( tri->ambientCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( idDrawVert );
		commands[i].baseInstance = i;
	}

	idDrawVert *ac = ( idDrawVert * )vertexCache.VertexPosition( staticVertex ? 1 : 2 );
	qglVertexAttribPointer( 0, 3, GL_FLOAT, false, sizeof( idDrawVert ), &ac->xyz );
	vertexCache.IndexPosition( staticIndex ? 1 : 2 );
	openGL4Renderer.BindSSBO( 0, ssboSize );

	qglMultiDrawElementsIndirect( GL_TRIANGLES, GL_INDEX_TYPE, commands, numDrawSurfs, 0 );
	openGL4Renderer.LockSSBO( ssboSize );
}

void GL4_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	GL_DEBUG_GROUP( FillDepthBuffer_MD, DEPTH );

	// draw all subview surfaces, which should be at the start of the sorted list, with the general purpose path
	int numSubviewSurfaces = 0;
	for( int i = 0; i < numDrawSurfs; ++i ) {
		if( drawSurfs[i]->material->GetSort() != SS_SUBVIEW ) {
			break;
		}
		++numSubviewSurfaces;
	}
	if( numSubviewSurfaces > 0 ) {
		RB_STD_FillDepthBuffer( drawSurfs, numSubviewSurfaces );
	}

	// divide list of draw surfs based on whether they are solid or perforated
	// and whether they use static or frame temp buffers.
	std::vector<drawSurf_t*> perforatedSurfaces;
	std::vector<drawSurf_t*> staticVertexStaticIndex;
	std::vector<drawSurf_t*> staticVertexFrameIndex;
	std::vector<drawSurf_t*> frameVertexFrameIndex;
	int numOffset = 0, numWeaponDepthHack = 0, numModelDepthHack = 0;
	for( int i = numSubviewSurfaces; i < numDrawSurfs; ++i ) {
		drawSurf_t *surf = drawSurfs[i];
		const srfTriangles_t *tri = surf->backendGeo;
		const idMaterial *material = surf->material;

		if( !material->IsDrawn() || material->Coverage() == MC_TRANSLUCENT ) {
			// translucent surfaces don't put anything in the depth buffer
			continue;
		}

		// some deforms may disable themselves by setting numIndexes = 0
		if( !tri->numIndexes ) {
			continue;
		}

		if( !tri->ambientCache ) {
			common->Printf( "GL4_FillDepthBuffer: !tri->ambientCache\n" );
			continue;
		}

		if( surf->material->GetSort() == SS_PORTAL_SKY && g_enablePortalSky.GetInteger() == 2 )
			continue;

		// get the expressions for conditionals / color / texcoords
		const float *regs = surf->shaderRegisters;
		// if all stages of a material have been conditioned off, don't do anything
		int stage;
		for( stage = 0; stage < material->GetNumStages(); stage++ ) {
			const shaderStage_t *pStage = material->GetStage( stage );
			// check the stage enable condition
			if( regs[pStage->conditionRegister] != 0 ) {
				break;
			}
		}
		if( stage == material->GetNumStages() ) {
			continue;
		}

		if( material->Coverage() == MC_PERFORATED || !tri->indexCache ) {
			// save for later drawing with the general purpose path
			perforatedSurfaces.push_back( surf );
			continue;
		}

		if( material->TestMaterialFlag( MF_POLYGONOFFSET ) )
			++numOffset;
		if( surf->space->weaponDepthHack )
			++numWeaponDepthHack;
		if( surf->space->modelDepthHack != 0 )
			++numModelDepthHack;

		if( !vertexCache.CacheIsStatic( tri->ambientCache ) ) {
			frameVertexFrameIndex.push_back( surf );
		}
		else {
			if( vertexCache.CacheIsStatic( tri->indexCache ) ) {
				staticVertexStaticIndex.push_back( surf );
			}
			else {
				staticVertexFrameIndex.push_back( surf );
			}
		}
	}

	if( numOffset != 0 || numWeaponDepthHack != 0 || numModelDepthHack != 0 )
		common->Printf( "Problematic surfaces: offset %d - weapon %d - model %d\n", numOffset, numWeaponDepthHack, numModelDepthHack );

	GL_State( GLS_DEPTHFUNC_LESS & GLS_COLORMASK & GLS_ALPHAMASK );
	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	GL4Program depthShaderMD = openGL4Renderer.GetShader( SHADER_DEPTH_FAST_MD );
	depthShaderMD.Activate();
	qglUniformMatrix4fv( SU_LOC_PROJ_MATRIX, 1, GL_FALSE, backEnd.viewDef->projectionMatrix );
	openGL4Renderer.BindDrawId( 1 );

	GL4_MultiDrawDepth( &staticVertexStaticIndex[0], staticVertexStaticIndex.size(), true, true );
	GL4_MultiDrawDepth( &staticVertexFrameIndex[0], staticVertexFrameIndex.size(), true, false );
	GL4_MultiDrawDepth( &frameVertexFrameIndex[0], frameVertexFrameIndex.size(), false, false );

	GL_CheckErrors();

	// draw all perforated surfaces with the general code path
	if( !perforatedSurfaces.empty() ) {
		RB_STD_FillDepthBuffer( &perforatedSurfaces[0], perforatedSurfaces.size() );
	}

	GL4Program::Unset();
}
