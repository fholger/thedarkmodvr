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

void GL4_GenericDepth( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	GL_DEBUG_GROUP( GenericDepth, DEPTH );

	GL4Program depthShader = openGL4Renderer.GetShader( SHADER_DEPTH_GENERIC );
	depthShader.Activate();
	depthShader.SetProjectionMatrix( SU_LOC_PROJ_MATRIX );
	depthShader.SetUniformMatrix4( SU_LOC_TEXTURE_MATRIX, mat4_identity.ToFloatPtr() );

	// if we have no clip planes, set a noclip plane
	if( !backEnd.viewDef->numClipPlanes ) {
		const float noClip[] = { 0, 0, 0, 1 };
		depthShader.SetUniform4( SU_LOC_CLIPPLANE, noClip );
	}

	// the first texture will be used for alpha tested surfaces
	GL_SelectTexture( 0 );
	qglEnableVertexAttribArray( 1 );

	for( int i = 0; i < numDrawSurfs; ++i ) {
		drawSurf_t *surf = drawSurfs[i];
		float color[4] = { 0, 0, 0, 1 };  // draw black by default

		const srfTriangles_t *tri = surf->backendGeo;
		const idMaterial *shader = surf->material;

		if( !shader->IsDrawn() || shader->Coverage() == MC_TRANSLUCENT || !tri->numIndexes) {
			continue;
		}

		if( surf->material->GetSort() == SS_PORTAL_SKY && g_enablePortalSky.GetInteger() == 2 )
			continue;

		if( !tri->ambientCache ) {
			common->Printf( "GL4_GenericDepth: !tri->ambientCache\n" );
			continue;
		}

		// get the expressions for conditionals / color / texcoords
		const float *regs = surf->shaderRegisters;

		// if all stages of a material have been conditioned off, don't do anything
		int stage;
		for( stage = 0; stage < shader->GetNumStages(); stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );
			// check the stage enable condition
			if( regs[pStage->conditionRegister] != 0 ) {
				break;
			}
		}
		if( stage == shader->GetNumStages() ) {
			continue;
		}

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
			idPlane	plane;
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane );
			depthShader.SetUniform4( SU_LOC_CLIPPLANE, plane.ToFloatPtr() );
		}

		if( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;
			depthShader.SetUniformMatrix4( SU_LOC_MODELVIEW_MATRIX, surf->space->modelViewMatrix );
		}

		// set polygon offset if necessary
		if( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			qglEnable( GL_POLYGON_OFFSET_FILL );
			qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
		}

		// subviews will just down-modulate the color buffer by overbright
		if( shader->GetSort() == SS_SUBVIEW ) {
			GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS );
			color[0] = color[1] = color[2] = ( 1.0 / backEnd.overBright );
		}

		idDrawVert *ac = ( idDrawVert * )vertexCache.VertexPosition( tri->ambientCache );
		qglVertexAttribPointer( 0, 3, GL_FLOAT, false, sizeof( idDrawVert ), &ac->xyz );
		qglVertexAttribPointer( 1, 2, GL_FLOAT, false, sizeof( idDrawVert ), ac->st.ToFloatPtr() );

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
			for( stage = 0; stage < shader->GetNumStages(); stage++ ) {
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
				color[3] = regs[pStage->color.registers[3]];

				// skip the entire stage if alpha would be black
				if( color[3] <= 0 ) {
					continue;
				}
				depthShader.SetUniform4( SU_LOC_COLOR, color );
				depthShader.SetUniform1(SU_LOC_ALPHATEST, regs[pStage->alphaTestRegister] );

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
					depthShader.SetUniformMatrix4( SU_LOC_TEXTURE_MATRIX, matrix );
				}

				// draw it
				RB_DrawElementsWithCounters( tri );

				// unset privatePolygonOffset if necessary
				if( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
					qglDisable( GL_POLYGON_OFFSET_FILL );
				}

				if( pStage->texture.hasMatrix ) {
					depthShader.SetUniformMatrix4( SU_LOC_TEXTURE_MATRIX, mat4_identity.ToFloatPtr() );
				}
			}

			if( !didDraw ) {
				drawSolid = true;
			}
		}

		// draw the entire surface solid
		if( drawSolid ) {
			depthShader.SetUniform4( SU_LOC_COLOR, color );
			depthShader.SetUniform1( SU_LOC_ALPHATEST, -1 ); // hint the glsl to skip texturing

			// draw it
			RB_DrawElementsWithCounters( tri );
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

	// draw all subview surfaces, which should be at the start of the sorted list, with the general purpose path
	int numSubviewSurfaces = 0;
	for( int i = 0; i < numDrawSurfs; ++i ) {
		if( drawSurfs[i]->material->GetSort() != SS_SUBVIEW ) {
			break;
		}
		++numSubviewSurfaces;
	}
	if( numSubviewSurfaces > 0 ) {
		GL4_GenericDepth( drawSurfs, numSubviewSurfaces );
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

	GL4Program depthShaderMD = openGL4Renderer.GetShader( SHADER_DEPTH_FAST_MD );
	depthShaderMD.Activate();
	depthShaderMD.SetProjectionMatrix( SU_LOC_PROJ_MATRIX );
	openGL4Renderer.BindDrawId( 1 );

	GL4_MultiDrawDepth( &staticVertexStaticIndex[0], staticVertexStaticIndex.size(), true, true );
	GL4_MultiDrawDepth( &staticVertexFrameIndex[0], staticVertexFrameIndex.size(), true, false );
	GL4_MultiDrawDepth( &frameVertexFrameIndex[0], frameVertexFrameIndex.size(), false, false );

	GL_CheckErrors();

	// draw all perforated surfaces with the general code path
	if( !perforatedSurfaces.empty() ) {
		GL4_GenericDepth( &perforatedSurfaces[0], perforatedSurfaces.size() );
	}

	GL4Program::Unset();
}
