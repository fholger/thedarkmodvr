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

struct DepthFastDrawData {
	float modelViewMatrix[16];
};

struct DepthGenericDrawData {
	float modelViewMatrix[16];
	float textureMatrix[16];
	idPlane clipPlane;
	idVec4 color;
	idVec4 alphaTest;
};

const int SU_LOC_PROJ_MATRIX = 0;
const int SU_LOC_MODELVIEW_MATRIX = 1;
const int SU_LOC_CLIPPLANE = 2;
const int SU_LOC_TEXTURE_MATRIX = 3;
const int SU_LOC_COLOR = 4;
const int SU_LOC_ALPHATEST = 5;


void GL4_MultiDrawDepth( drawSurf_t **drawSurfs, int numDrawSurfs, bool staticVertex, bool staticIndex ) {
	if( numDrawSurfs == 0 ) {
		return;
	}

	GL_DEBUG_GROUP( MultiDrawDepth, DEPTH );

	DrawElementsIndirectCommand *commands = openGL4Renderer.ReserveCommandBuffer( numDrawSurfs );
	GLuint ssboSize = sizeof( DepthFastDrawData ) * numDrawSurfs;
	DepthFastDrawData *drawData = ( DepthFastDrawData* )openGL4Renderer.ReserveSSBO( ssboSize );

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

void GL4_GenericDepth( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	GL_DEBUG_GROUP( GenericDepth, DEPTH );

	GL4Program depthShader = openGL4Renderer.GetShader( SHADER_DEPTH_GENERIC );
	depthShader.Activate();
	depthShader.SetProjectionMatrix( SU_LOC_PROJ_MATRIX );

	DepthGenericDrawData drawData;
	DrawElementsIndirectCommand command;
	memcpy( drawData.textureMatrix, mat4_identity.ToFloatPtr(), sizeof( drawData.textureMatrix ) );

	// if we have no clip planes, set a noclip plane
	if( !backEnd.viewDef->numClipPlanes ) {
		drawData.clipPlane.ToVec4().Set( 0, 0, 0, 1 );
	}

	qglEnableVertexAttribArray( 1 );
	qglVertexAttribFormat( 0, 3, GL_FLOAT, false, offsetof( idDrawVert, xyz ) );
	qglVertexAttribFormat( 1, 2, GL_FLOAT, false, offsetof( idDrawVert, st ) );
	qglBindVertexBuffer( 0, vertexCache.StaticVertexBuffer(), 0, sizeof( idDrawVert ) );
	qglBindVertexBuffer( 1, vertexCache.FrameVertexBuffer(), 0, sizeof( idDrawVert ) );
	qglVertexAttribBinding( 0, 0 );
	qglVertexAttribBinding( 1, 0 );
	GLuint boundVertexBuffer = 0;

	// the first texture will be used for alpha tested surfaces
	GL_SelectTexture( 0 );
	openGL4Renderer.BindUBO( 0 );

	for( int i = 0; i < numDrawSurfs; ++i ) {
		drawSurf_t *surf = drawSurfs[i];
		drawData.color.Set( 0, 0, 0, 1 );  // draw black by default

		const srfTriangles_t *tri = surf->backendGeo;
		const idMaterial *shader = surf->material;

		// change the scissor if needed
		if( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			backEnd.currentScissor = surf->scissorRect;
			qglScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}

		// update the clip plane if needed
		if( backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], drawData.clipPlane );
		}

		if( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;
			memcpy( drawData.modelViewMatrix, surf->space->modelViewMatrix, sizeof( drawData.modelViewMatrix ) );
		}

		// set polygon offset if necessary
		if( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			qglEnable( GL_POLYGON_OFFSET_FILL );
			qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
		}

		// subviews will just down-modulate the color buffer by overbright
		if( shader->GetSort() == SS_SUBVIEW ) {
			GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS );
			drawData.color[0] = drawData.color[1] = drawData.color[2] = ( 1.0 / backEnd.overBright );
		}

		vertexCache.IndexPosition( tri->indexCache );
		if( vertexCache.CacheIsStatic(tri->ambientCache) != boundVertexBuffer ) {
			boundVertexBuffer ^= 1;
			qglVertexAttribBinding( 0, boundVertexBuffer );
			qglVertexAttribBinding( 1, boundVertexBuffer );
		}
		command.count = tri->numIndexes;
		command.instanceCount = 1;
		command.baseInstance = 0;
		command.baseVertex = ( ( tri->ambientCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( idDrawVert );
		command.firstIndex = ( ( tri->indexCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( glIndex_t );

		bool drawSolid = false;

		if( shader->Coverage() == MC_OPAQUE ) {
			drawSolid = true;
		}

		// we may have multiple alpha tested stages
		if( shader->Coverage() == MC_PERFORATED ) {
			// if the only alpha tested stages are condition register omitted,
			// draw a normal opaque surface
			bool didDraw = false;

			// perforated surfaces may have multiple alpha tested stages
			const float *regs = surf->shaderRegisters;
			for( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
				const shaderStage_t *pStage = shader->GetStage( stage );

				if( !pStage->hasAlphaTest ) {
					continue;
				}

				// check the stage enable condition
				if( regs[pStage->conditionRegister] == 0 ) {
					continue;
				}

				// if we at least tried to draw an alpha tested stage,
				// we won't draw the opaque surface
				didDraw = true;

				// set the alpha modulate
				drawData.color[3] = regs[pStage->color.registers[3]];

				// skip the entire stage if alpha would be black
				if( drawData.color[3] <= 0 ) {
					continue;
				}
				drawData.alphaTest.x = regs[pStage->alphaTestRegister];

				// bind the texture
				pStage->texture.image->Bind();

				// set privatePolygonOffset if necessary
				if( pStage->privatePolygonOffset ) {
					qglEnable( GL_POLYGON_OFFSET_FILL );
					qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
				}

				// set the texture matrix if needed
				if( pStage->texture.hasMatrix ) {
					float matrix[16];
					RB_GetShaderTextureMatrix( surf->shaderRegisters, &pStage->texture, matrix );
					memcpy( drawData.textureMatrix, matrix, sizeof( drawData.textureMatrix ) );
				}

				// draw it
				openGL4Renderer.UpdateUBO( &drawData, sizeof( DepthGenericDrawData ) );
				qglDrawElementsIndirect( GL_TRIANGLES, GL_INDEX_TYPE, &command );
				//RB_DrawElementsWithCounters( tri );

				// unset privatePolygonOffset if necessary
				if( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
					qglDisable( GL_POLYGON_OFFSET_FILL );
				}

				if( pStage->texture.hasMatrix ) {
					memcpy( drawData.textureMatrix, mat4_identity.ToFloatPtr(), sizeof( drawData.textureMatrix ) );
				}
			}

			if( !didDraw ) {
				drawSolid = true;
			}
		}

		// draw the entire surface solid
		if( drawSolid ) {
			drawData.alphaTest.x = -1;

			// draw it
			openGL4Renderer.UpdateUBO( &drawData, sizeof( DepthGenericDrawData ) );
			qglDrawElementsIndirect( GL_TRIANGLES, GL_INDEX_TYPE, &command );
			//RB_DrawElementsWithCounters( tri );
		}

		// reset polygon offset
		if( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			qglDisable( GL_POLYGON_OFFSET_FILL );
		}

		// reset blending
		if( shader->GetSort() == SS_SUBVIEW ) {
			GL_State( GLS_DEPTHFUNC_LESS );
		}
	}

	qglDisableVertexAttribArray( 1 );
}

void GL4_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	// if we are just doing 2D rendering, no need to fill the depth buffer
	if( !backEnd.viewDef->viewEntitys ) {
		return;
	}

	GL_DEBUG_GROUP( FillDepthBuffer_MD, DEPTH );

	GL_State( GLS_DEPTHFUNC_LESS & GLS_COLORMASK & GLS_ALPHAMASK );
	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	// divide list of draw surfs into the separate batches
	std::vector<drawSurf_t*> subViewSurfaces;
	std::vector<drawSurf_t*> staticVertexStaticIndex;
	std::vector<drawSurf_t*> staticVertexFrameIndex;
	std::vector<drawSurf_t*> frameVertexFrameIndex;
	std::vector<drawSurf_t*> remainingSurfaces;
	for( int i = 0; i < numDrawSurfs; ++i ) {
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

		if( !tri->indexCache ) {
			common->Printf( "GL4_FillDepthBuffer: !tri->indexCache\n" );
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

		if( material->GetSort() == SS_SUBVIEW ) {
			// subview surfaces need to be rendered first in a generic pass (due to mirror plane clipping).
			subViewSurfaces.push_back( surf );
			continue;
		}

		if( material->Coverage() == MC_PERFORATED || material->TestMaterialFlag( MF_POLYGONOFFSET ) || surf->space->weaponDepthHack || surf->space->modelDepthHack != 0) {
			// these are objects that can't be handled by the fast depth pass. 
			// they will be postponed and rendered in a generic pass.
			remainingSurfaces.push_back( surf );
			continue;
		}

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

	if( !subViewSurfaces.empty() ) {
		GL4_GenericDepth( subViewSurfaces.data(), subViewSurfaces.size() );
	}


	// sort static objects by view distance to profit more from early Z
	std::sort( staticVertexStaticIndex.begin(), staticVertexStaticIndex.end(), []( const drawSurf_t* a, const drawSurf_t* b ) -> bool {
		float distA = ( a->space->entityDef->parms.origin - backEnd.viewDef->renderView.vieworg ).LengthSqr();
		float distB = ( b->space->entityDef->parms.origin - backEnd.viewDef->renderView.vieworg ).LengthSqr();
		return distA < distB;
	} );


	GL4Program depthShaderMD = openGL4Renderer.GetShader( SHADER_DEPTH_FAST_MD );
	depthShaderMD.Activate();
	depthShaderMD.SetProjectionMatrix( SU_LOC_PROJ_MATRIX );
	openGL4Renderer.BindDrawId( 1 );

	GL4_MultiDrawDepth( &staticVertexStaticIndex[0], staticVertexStaticIndex.size(), true, true );
	GL4_MultiDrawDepth( &staticVertexFrameIndex[0], staticVertexFrameIndex.size(), true, false );
	GL4_MultiDrawDepth( &frameVertexFrameIndex[0], frameVertexFrameIndex.size(), false, false );

	// draw all remaining surfaces with the general code path
	if( !remainingSurfaces.empty() ) {
		GL4_GenericDepth( &remainingSurfaces[0], remainingSurfaces.size() );
	}

	GL_CheckErrors();

	GL4Program::Unset();
}
