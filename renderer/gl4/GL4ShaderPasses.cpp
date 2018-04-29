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
#include "../glsl.h"
#include "OcclusionSystem.h"

struct ShaderPassDrawData {
	float modelMatrix[16];
	float mvpMatrix[16];
	float textureMatrix[16];
	idVec4 localEyePos;
	idVec4 colorAdd;
	idVec4 colorMul;
	idVec4 vertexColor;
	idVec4 screenTex;
};

void GL4_RenderShaderPasses_OldStage(const shaderStage_t * pStage, drawSurf_t * surf, ShaderPassDrawData &drawData) {
	drawData.vertexColor = idVec4( 0, 0, 0, 0 );
	drawData.screenTex = idVec4( 0, 0, 0, 0 );
	memcpy( drawData.textureMatrix, mat4_identity.ToFloatPtr(), sizeof( drawData.textureMatrix ) );
	// set the color
	idVec4 color;
	const float	*regs = surf->shaderRegisters;
	color[0] = regs[pStage->color.registers[0]];
	color[1] = regs[pStage->color.registers[1]];
	color[2] = regs[pStage->color.registers[2]];
	color[3] = regs[pStage->color.registers[3]];

	// skip the entire stage if an add would be black
	if( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE )
		&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
		return;
	}

	// skip the entire stage if a blend would be completely transparent
	if( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
		&& color[3] <= 0 ) {
		return;
	}

	const idVec4 zero ( r_ambient_testadd.GetFloat(), r_ambient_testadd.GetFloat(), r_ambient_testadd.GetFloat(), 0 );
	const idVec4 one ( 1, 1, 1, 1 );
	const idVec4 negOne ( -color[0], -color[1], -color[2], -1 );

	switch( pStage->texture.texgen ) {
	case TG_SKYBOX_CUBE: case TG_WOBBLESKY_CUBE:
		// TODO: not implemented
		/*qglEnableVertexAttribArray( 8 );
		qglVertexAttribPointer( 8, 3, GL_FLOAT, false, 0, vertexCache.VertexPosition( surf->dynamicTexCoords ) );
		cubeMapShader.Use();
		break;*/
		return;
	case TG_REFLECT_CUBE:
		drawData.vertexColor = color;
		//break;
		// TODO: not implemented
		return;
	case TG_SCREEN:
		drawData.vertexColor = color;
		drawData.screenTex.x = 1;
	default:
		openGL4Renderer.EnableVertexAttribs( { VA_POSITION, VA_TEXCOORD, VA_COLOR } );
		openGL4Renderer.GetShader( SHADER_OLDSTAGE ).Activate();
		switch( pStage->vertexColor ) {
		case SVC_IGNORE:
			drawData.colorMul = zero;
			drawData.colorAdd = color;
			break;
		case SVC_MODULATE:
			// select the vertex color source
			drawData.colorMul = color;
			drawData.colorAdd = zero;
			break;
		case SVC_INVERSE_MODULATE:
			// select the vertex color source
			drawData.colorMul = negOne;
			drawData.colorAdd = color;
			break;
		}
	}

	// TODO: Reflect Cube RB_PrepareStageTexturing( pStage, surf, ac );
	// set privatePolygonOffset if necessary
	if( pStage->privatePolygonOffset ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
	}

	// set the texture matrix if needed
	if( pStage->texture.hasMatrix ) {
		RB_GetShaderTextureMatrix( surf->shaderRegisters, &pStage->texture, drawData.textureMatrix );
	}

	// bind the texture
	GL_SelectTexture( 0 );
	if( pStage->texture.cinematic ) {
		globalImages->blackImage->Bind();
	} else if( pStage->texture.image ) {
		pStage->texture.image->Bind();
	}

	// set the state
	GL_State( pStage->drawStateBits );

	// draw it
	const srfTriangles_t	*tri = surf->backendGeo;
	openGL4Renderer.UpdateUBO( &drawData, sizeof( ShaderPassDrawData ) );
	openGL4Renderer.BindVertexBuffer( vertexCache.CacheIsStatic( tri->ambientCache ) );
	int baseVertex = ( ( tri->ambientCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( idDrawVert );
	const void* indexes = tri->indexes;
	if( tri->indexCache ) {
		indexes = vertexCache.IndexPosition( tri->indexCache );
	} else {
		vertexCache.UnbindIndex();
	}
	qglDrawElementsBaseVertex( GL_TRIANGLES, tri->numIndexes, GL_INDEX_TYPE, indexes, baseVertex );

	// unset privatePolygonOffset if necessary
	if( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );

	openGL4Renderer.EnableVertexAttribs( { VA_POSITION } );
	qglUseProgram( 0 );
}

void GL4_RenderShaderPasses(drawSurf_t * surf, ShaderPassDrawData& drawData) {

	const srfTriangles_t *tri = surf->backendGeo;
	const idMaterial *shader = surf->material;

	float modelView[16];

	if( !shader->HasAmbient() )
		return;

	if( shader->IsPortalSky() )  // NB TDM portal sky does not use this flag or whatever mechanism 
		return;					   // it used to support. Our portalSky is drawn in this procedure using
	// the skybox image captured in _currentRender. -- SteveL working on #4182

	if( surf->material->GetSort() == SS_PORTAL_SKY && g_enablePortalSky.GetInteger() == 2 )
		return;

	GL_DEBUG_GROUP( RenderShaderPasses_GL4, SHADER_PASS );

	// calculate MVP matrix
	if( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;
		memcpy( drawData.modelMatrix, surf->space->modelMatrix, sizeof( drawData.modelMatrix ) );
		if( backEnd.viewDef->renderView.viewEyeBuffer != 0 ) {
			return;
			myGlMultMatrix( surf->space->modelMatrix, backEnd.viewMatrix, modelView );
			myGlMultMatrix( modelView, backEnd.viewDef->projectionMatrix, drawData.mvpMatrix );
		} else {
			myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, drawData.mvpMatrix );
		}
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewOrigin, drawData.localEyePos.ToVec3() );
		drawData.localEyePos.w = 1;
	}

	// change the scissor if needed
	if( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		qglScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	// check whether we're drawing a soft particle surface #3878
	const bool soft_particle = ( surf->dsFlags & DSF_SOFT_PARTICLE ) != 0;

	// get the expressions for conditionals / color / texcoords
	const float *regs = surf->shaderRegisters;

	// set face culling appropriately
	GL_Cull( shader->GetCullType() );

	// set polygon offset if necessary
	if( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	if( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
	}

	if( surf->space->modelDepthHack != 0.0f && !soft_particle ) // #3878 soft particles don't want modelDepthHack, which is 
	{                                                            // an older way to slightly "soften" particles
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}

	openGL4Renderer.BindVertexBuffer( vertexCache.CacheIsStatic( tri->ambientCache ) );

	for( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );

		// check the enable condition
		if( regs[pStage->conditionRegister] == 0 )
			continue;

		// skip the stages involved in lighting
		if( pStage->lighting != SL_AMBIENT )
			continue;

		// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
		if( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) )
			continue;

		// see if we are a new-style stage
		newShaderStage_t *newStage = pStage->newStage;
		if( newStage ) {
			// TODO: not implemented
			//RB_STD_T_RenderShaderPasses_NewStage( ac, pStage, surf );
			continue;
		}
		if( soft_particle && surf->particle_radius > 0.0f ) {
			// TODO: not implemented
			//RB_STD_T_RenderShaderPasses_SoftParticle( ac, pStage, surf );
			continue;
		}
		GL4_RenderShaderPasses_OldStage( pStage, surf, drawData );
	}

	// reset polygon offset
	if( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
	if( surf->space->weaponDepthHack || ( !soft_particle && surf->space->modelDepthHack != 0.0f ) ) {
		RB_LeaveDepthHack();
	}
}

int GL4_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs, bool afterFog ) {

	GL_DEBUG_GROUP( DrawShaderPasses_GL4, SHADER_PASS );
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

	ShaderPassDrawData drawData;

	openGL4Renderer.PrepareVertexAttribs();
	openGL4Renderer.EnableVertexAttribs( { VA_POSITION } );

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

		if( r_useOcclusionCulling.GetBool() && drawSurfs[i]->space && occlusionSystem.WasEntityCulledLastFrame(drawSurfs[i]->space->entityIndex) ) {
			continue;
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS && !backEnd.currentRenderCopied )
			break;

		if( drawSurfs[i]->material->GetSort() == SS_AFTER_FOG && !afterFog )
			break;

		/*if( !drawSurfs[i]->backendGeo->indexCache ) {
			common->Printf( "GL4_DrawShaderPasses: !tri->indexCache\n" );
			continue;
		}*/
		if( !drawSurfs[i]->backendGeo->ambientCache ) {
			common->Printf( "GL4_DrawShaderPasses: !tri->ambientCache\n" );
			continue;
		}

		GL4_RenderShaderPasses( drawSurfs[i], drawData );
	}

	GL_Cull( CT_FRONT_SIDED );

	return i;
}
