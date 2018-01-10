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

idCVar r_skipInteractionFastPath( "r_skipInteractionFastPath", "0", CVAR_RENDERER | CVAR_BOOL, "" );

void RB_GLSL_CreateDrawInteractions( const drawSurf_t *surf );
void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float *textureMatrix );

enum InteractionTextures {
	TEX_NORMAL = 0,
	TEX_LIGHT_FALLOFF = 1,
	TEX_LIGHT_PROJECTION = 2,
	CUBE_LIGHT_PROJECTION = 3,
	TEX_DIFFUSE = 4,
	TEX_SPECULAR = 5
};

struct StencilDrawData {
	float  mvpMatrix[16];
	idVec4 lightOrigin;
};

struct InteractionDrawData {
	float modelMatrix[16];
	idVec4 bumpMatrix[2];
	idVec4 diffuseMatrix[2];
	idVec4 specularMatrix[2];
	idPlane lightProjection[4];
	idVec4 colorModulate;
	idVec4 colorAdd;
	idVec4 lightOrigin;
	idVec4 viewOrigin;
	idVec4 diffuseColor;
	idVec4 specularColor;
	idVec4 cubic;  // technically just a bool, but expanded for alignment
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

		myGlMultMatrix( drawSurf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, drawData[count].mvpMatrix );
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

static void GL4_DrawSingleInteraction( InteractionDrawData &drawData, drawInteraction_t * din ) {
	if( din->bumpImage == NULL ) {
		// stage wasn't actually an interaction
		return;
	}

	if( din->diffuseImage == NULL || r_skipDiffuse.GetBool() ) {
		// this isn't a YCoCg black, but it doesn't matter, because
		// the diffuseColor will also be 0
		din->diffuseImage = globalImages->blackImage;
	}
	if( din->specularImage == NULL || r_skipSpecular.GetBool() || din->ambientLight ) {
		din->specularImage = globalImages->blackImage;
	}
	if( r_skipBump.GetBool() ) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	const bool diffuseIsBlack = ( din->diffuseImage == globalImages->blackImage )
		|| ( ( drawData.diffuseColor[0] <= 0 ) && ( drawData.diffuseColor[1] <= 0 ) && ( drawData.diffuseColor[2] <= 0 ) );
	const bool specularIsBlack = ( din->specularImage == globalImages->blackImage )
		|| ( ( drawData.specularColor[0] <= 0 ) && ( drawData.specularColor[1] <= 0 ) && ( drawData.specularColor[2] <= 0 ) );
	if( diffuseIsBlack && specularIsBlack ) {
		return;
	}

	static const idVec4 zero( 0, 0, 0, 0 );
	static const idVec4 one( 1, 1, 1, 1 );
	static const idVec4 negOne( -1, -1, -1, 1 );
	switch( din->vertexColor ) {
	case SVC_IGNORE:
		drawData.colorModulate = zero;
		drawData.colorAdd = one;
		break;
	case SVC_MODULATE:
		drawData.colorModulate = one;
		drawData.colorAdd = zero;
		break;
	case SVC_INVERSE_MODULATE:
		drawData.colorModulate = negOne;
		drawData.colorAdd = one;
		break;
	}

	// texture 0 will be the per-surface bump map
	GL_SelectTexture( TEX_NORMAL );
	din->bumpImage->Bind();

	// texture 3 is the per-surface diffuse map
	GL_SelectTexture( TEX_DIFFUSE );
	din->diffuseImage->Bind();

	// texture 4 is the per-surface specular map
	GL_SelectTexture( TEX_SPECULAR );
	din->specularImage->Bind();

	const srfTriangles_t *tri = din->surf->backendGeo;
	int baseVertex = ( ( tri->ambientCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( idDrawVert );
	//openGL4Renderer.BindVertexBuffer( vertexCache.CacheIsStatic( tri->ambientCache ) );
	openGL4Renderer.UpdateUBO( &drawData, sizeof( drawData ) );
	qglDrawElementsBaseVertex( GL_TRIANGLES, tri->numIndexes, GL_INDEX_TYPE, vertexCache.IndexPosition( tri->indexCache ), baseVertex );
}

void GL4_SetDrawInteraction( const shaderStage_t *surfaceStage, const float *surfaceRegs,
	idImage **image, idVec4 matrix[2], float color[4] ) {
	*image = surfaceStage->texture.image;

	if( surfaceStage->texture.hasMatrix ) {
		matrix[0][0] = surfaceRegs[surfaceStage->texture.matrix[0][0]];
		matrix[0][1] = surfaceRegs[surfaceStage->texture.matrix[0][1]];
		matrix[0][2] = 0;
		matrix[0][3] = surfaceRegs[surfaceStage->texture.matrix[0][2]];

		matrix[1][0] = surfaceRegs[surfaceStage->texture.matrix[1][0]];
		matrix[1][1] = surfaceRegs[surfaceStage->texture.matrix[1][1]];
		matrix[1][2] = 0;
		matrix[1][3] = surfaceRegs[surfaceStage->texture.matrix[1][2]];

		// we attempt to keep scrolls from generating incredibly large texture values, but
		// center rotations and center scales can still generate offsets that need to be > 1
		if( matrix[0][3] < -40.0f || matrix[0][3] > 40.0f ) {
			matrix[0][3] -= ( int )matrix[0][3];
		}
		if( matrix[1][3] < -40.0f || matrix[1][3] > 40.0f ) {
			matrix[1][3] -= ( int )matrix[1][3];
		}
	}
	else {
		matrix[0][0] = 1;
		matrix[0][1] = 0;
		matrix[0][2] = 0;
		matrix[0][3] = 0;

		matrix[1][0] = 0;
		matrix[1][1] = 1;
		matrix[1][2] = 0;
		matrix[1][3] = 0;
	}

	if( color ) {
		color[0] = surfaceRegs[surfaceStage->color.registers[0]];
		color[1] = surfaceRegs[surfaceStage->color.registers[1]];
		color[2] = surfaceRegs[surfaceStage->color.registers[2]];
		color[3] = surfaceRegs[surfaceStage->color.registers[3]];
	}
}

void GL4_RenderSurfaceInteractions(const drawSurf_t * surf, InteractionDrawData& drawData, const shaderStage_t * lightStage, const idVec4& lightColor) {
	GL_DEBUG_GROUP( SurfaceInteractions_GL4, INTERACTION );

	if( !surf->backendGeo->indexCache ) {
		common->Printf( "GL4_RenderSurfaceInteractions: !tri->indexCache\n" );
		return;
	}
	if( !surf->backendGeo->ambientCache ) {
		common->Printf( "GL4_RenderSurfaceInteractions: !tri->ambientCache\n" );
		return;
	}

	const viewLight_t *vLight = backEnd.vLight;
	const idMaterial* lightShader = vLight->lightShader;
	const idMaterial * surfaceShader = surf->material;
	const float * surfaceRegs = surf->shaderRegisters;
	const srfTriangles_t *tri = surf->backendGeo;
	int baseVertex = ( ( tri->ambientCache >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK ) / sizeof( idDrawVert );
	openGL4Renderer.BindVertexBuffer( vertexCache.CacheIsStatic( tri->ambientCache ) );
	vertexCache.VertexPosition( tri->ambientCache );
	// TODO: this is a hack because without it for some reason access to the tangent attribute is reported as "buffer is mapped"
	qglVertexAttribPointer( VA_TANGENT, 3, GL_FLOAT, GL_FALSE, sizeof( idDrawVert ), ( void* )offsetof( idDrawVert, tangents ) );

	if( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;
		// tranform the light/view origin into model local space
		R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, drawData.lightOrigin.ToVec3() );
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, drawData.viewOrigin.ToVec3() );
		drawData.lightOrigin[3] = 0;
		drawData.viewOrigin[3] = 1;

		// transform the light project into model local space
		for( int i = 0; i < 4; i++ ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[i], drawData.lightProjection[i] );
		}

		// optionally multiply the local light projection by the light texture matrix
		if( lightStage->texture.hasMatrix ) {
			RB_BakeTextureMatrixIntoTexgen( drawData.lightProjection, backEnd.lightTextureMatrix );
		}

		// copy model matrix
		memcpy( drawData.modelMatrix, surf->space->modelMatrix, sizeof( drawData.modelMatrix ) );
		openGL4Renderer.GetShader( SHADER_INTERACTION_SIMPLE ).SetModelViewProjectionMatrix( 0, surf->space );
	}

	// check for the fast path
	if( surfaceShader->GetFastPathBumpImage() && !r_skipInteractionFastPath.GetBool() ) {
		GL_SelectTexture( TEX_NORMAL );
		surfaceShader->GetFastPathBumpImage()->Bind();

		// texture 3 is the per-surface diffuse map
		GL_SelectTexture( TEX_DIFFUSE );
		surfaceShader->GetFastPathDiffuseImage()->Bind();

		// texture 4 is the per-surface specular map
		GL_SelectTexture( TEX_SPECULAR );
		surfaceShader->GetFastPathSpecularImage()->Bind();

		openGL4Renderer.UpdateUBO( &drawData, sizeof( drawData ) );
		qglDrawElementsBaseVertex( GL_TRIANGLES, tri->numIndexes, GL_INDEX_TYPE, vertexCache.IndexPosition( tri->indexCache ), baseVertex );

		return;
	}

	drawInteraction_t inter;
	inter.surf = surf;
	inter.bumpImage = NULL;
	inter.specularImage = NULL;
	inter.diffuseImage = NULL;
	drawData.diffuseColor[0] = drawData.diffuseColor[1] = drawData.diffuseColor[2] = drawData.diffuseColor[3] = 0;
	drawData.specularColor[0] = drawData.specularColor[1] = drawData.specularColor[2] = drawData.specularColor[3] = 0;

	// go through the individual stages
	//
	// This is somewhat arcane because of the old support for video cards that had to render
	// interactions in multiple passes.
	for( int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++ ) {
		const shaderStage_t	*surfaceStage = surfaceShader->GetStage( surfaceStageNum );

		switch( surfaceStage->lighting ) {
		case SL_AMBIENT: {
			// ignore ambient stages while drawing interactions
			break;
		}
		case SL_BUMP: {
			// ignore stage that fails the condition
			if( !surfaceRegs[surfaceStage->conditionRegister] ) {
				break;
			}
			// draw any previous interaction
			if( inter.bumpImage ) {
				GL4_DrawSingleInteraction( drawData, &inter );
			}
			inter.diffuseImage = NULL;
			inter.specularImage = NULL;
			GL4_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL );
			break;
		}
		case SL_DIFFUSE: {
			// ignore stage that fails the condition
			if( !surfaceRegs[surfaceStage->conditionRegister] ) {
				break;
			}
			// draw any previous interaction
			if( inter.diffuseImage ) {
				GL4_DrawSingleInteraction( drawData, &inter );
			}
			GL4_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage,
				drawData.diffuseMatrix, drawData.diffuseColor.ToFloatPtr() );
			drawData.diffuseColor[0] *= lightColor[0];
			drawData.diffuseColor[1] *= lightColor[1];
			drawData.diffuseColor[2] *= lightColor[2];
			drawData.diffuseColor[3] *= lightColor[3];
			inter.vertexColor = surfaceStage->vertexColor;
			break;
		}
		case SL_SPECULAR: {
			// ignore stage that fails the condition
			if( !surfaceRegs[surfaceStage->conditionRegister] ) {
				break;
			}
			// nbohr1more: #4292 nospecular and nodiffuse fix
			if( vLight->lightDef->parms.noSpecular ) {
				break;
			}
			// draw any previous interaction
			if( inter.specularImage ) {
				GL4_DrawSingleInteraction( drawData, &inter );
			}
			GL4_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage,
				drawData.specularMatrix, drawData.specularColor.ToFloatPtr() );
			drawData.specularColor[0] *= lightColor[0];
			drawData.specularColor[1] *= lightColor[1];
			drawData.specularColor[2] *= lightColor[2];
			drawData.specularColor[3] *= lightColor[3];
			inter.vertexColor = surfaceStage->vertexColor;
			break;
		}
		}
	}
	// render the final interaction
	GL4_DrawSingleInteraction( drawData, &inter );
}

void GL4_RenderInteractions( const drawSurf_t *surfList ) {
	if( !surfList )
		return;

	viewLight_t *vLight = backEnd.vLight;
	const idMaterial * lightShader = vLight->lightShader;
	const float * lightRegs = vLight->shaderRegisters;

	// TODO: ambient interactions not implemented in shader yet
	if( lightShader->IsAmbientLight() )
		return;

	if( lightShader->IsAmbientLight() && r_skipAmbient.GetInteger() == 2 ) {
		return;
	}

	GL_DEBUG_GROUP( RenderInteractions_GL4, INTERACTION );

	GL_SelectTexture( 0 );
	qglActiveTexture( GL_TEXTURE0 );
	qglBindTexture( GL_TEXTURE_2D, 0 );
	qglBindTexture( GL_TEXTURE_CUBE_MAP, 0 );
	qglDisable( GL_TEXTURE_CUBE_MAP );
	// perform setup here that will be constant for all interactions
	GL4Program interactionShader = openGL4Renderer.GetShader( SHADER_INTERACTION_SIMPLE );
	interactionShader.Activate();
	openGL4Renderer.BindUBO( 0 );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );
	openGL4Renderer.PrepareVertexAttribs();
	openGL4Renderer.EnableVertexAttribs( { VA_POSITION, VA_TEXCOORD, VA_NORMAL, VA_TANGENT, VA_BITANGENT, VA_COLOR } );
	
	InteractionDrawData drawData;

	// sort simple surfaces first: they will be able to use the fast path
	std::vector<const drawSurf_t*> allSurfaces;
	std::vector<const drawSurf_t*> complexSurfaces;
	for( const drawSurf_t * walk = surfList; walk != NULL; walk = walk->nextOnLight ) {
		if( walk->material->GetFastPathBumpImage() ) {
			allSurfaces.push_back( walk );
		} else {
			complexSurfaces.push_back( walk );
		}
	}
	allSurfaces.insert( allSurfaces.end(), complexSurfaces.begin(), complexSurfaces.end() );

	bool lightDepthBoundsDisabled = false;

	for( int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); ++lightStageNum ) {
		const shaderStage_t *lightStage = lightShader->GetStage( lightStageNum );

		// ignore stages that fail the condition
		if( !lightRegs[lightStage->conditionRegister] ) {
			continue;
		}

		// backEnd.lightScale is calculated so that lightColor[] will never exceed tr.backEndRendererMaxLight
		const idVec4 lightColor (
			backEnd.lightScale * lightRegs[lightStage->color.registers[0]],
			backEnd.lightScale * lightRegs[lightStage->color.registers[1]],
			backEnd.lightScale * lightRegs[lightStage->color.registers[2]],
			lightRegs[lightStage->color.registers[3]] );

		if( lightStage->texture.hasMatrix ) {
			RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, backEnd.lightTextureMatrix );
		}
		
		GL_SelectTexture( TEX_LIGHT_FALLOFF );
		vLight->falloffImage->Bind();
		GL_SelectTexture( TEX_LIGHT_PROJECTION );
		lightStage->texture.image->Bind();
		GL_SelectTexture( CUBE_LIGHT_PROJECTION );
		lightStage->texture.image->Bind();

		// setup for the fast path first
		drawData.diffuseColor = lightColor;
		drawData.specularColor = lightColor * 2;
		drawData.bumpMatrix[0] = drawData.diffuseMatrix[0] = drawData.specularMatrix[0] = idVec4( 1, 0, 0, 0 );
		drawData.bumpMatrix[1] = drawData.diffuseMatrix[1] = drawData.specularMatrix[1] = idVec4( 0, 1, 0, 0 );
		drawData.colorAdd = idVec4( 1, 1, 1, 1 );
		drawData.colorModulate = idVec4( 0, 0, 0, 0 );
		drawData.cubic.x = lightShader->IsCubicLight();

		// even if the space does not change between light stages, each light stage may need a different lightTextureMatrix baked in
		backEnd.currentSpace = NULL;

		for( const drawSurf_t *surf : allSurfaces ) {
			if( surf->dsFlags & DSF_SHADOW_MAP_ONLY )
				continue;
			// set the vertex pointers
			openGL4Renderer.BindVertexBuffer( vertexCache.CacheIsStatic( surf->backendGeo->ambientCache ) );

			// turn off the light depth bounds test if this model is rendered with a depth hack
			if( surf->space != backEnd.currentSpace && r_useDepthBoundsTest.GetBool() ) {
				if( !surf->space->weaponDepthHack && surf->space->modelDepthHack == 0.0f ) {
					if( lightDepthBoundsDisabled ) {
						GL_DepthBoundsTest( vLight->scissorRect.zmin, vLight->scissorRect.zmax );
						lightDepthBoundsDisabled = false;
					}
				}
				else {
					if( !lightDepthBoundsDisabled ) {
						GL_DepthBoundsTest( 0.0f, 0.0f );
						lightDepthBoundsDisabled = true;
					}
				}
			}

			GL4_RenderSurfaceInteractions( surf, drawData, lightStage, lightColor );
		}
	}


	openGL4Renderer.EnableVertexAttribs( { VA_POSITION } );

	// disable features
	if( r_softShadowsQuality.GetBool() && !backEnd.viewDef->IsLightGem() || r_shadows.GetInteger() == 2 ) {
		GL_SelectTexture( 6 );
		globalImages->BindNull();
		GL_SelectTexture( 7 );
		globalImages->BindNull();
	}

	GL_SelectTexture( 4 );
	globalImages->BindNull();

	GL_SelectTexture( 3 );
	globalImages->BindNull();

	GL_SelectTexture( 2 );
	globalImages->BindNull();

	GL_SelectTexture( 1 );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	globalImages->BindNull();

	qglUseProgram( 0 );
	GL_CheckErrors();
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
	GL4_RenderInteractions( backEnd.vLight->localInteractions );
	GL4_StencilShadowPass( backEnd.vLight->localShadows );
	GL4_RenderInteractions( backEnd.vLight->globalInteractions );
}

void GL4_DrawInteractions() {
	if( r_skipInteractions.GetBool() ) {
		return;
	}

	GL_DEBUG_GROUP( DrawInteractions_GL4, INTERACTION );
	
	// for each light, perform adding and shadowing
	for( backEnd.vLight = backEnd.viewDef->viewLights; backEnd.vLight; backEnd.vLight = backEnd.vLight->next ) {
		// do fogging later
		if( backEnd.vLight->lightShader->IsFogLight() || backEnd.vLight->lightShader->IsBlendLight() )
			continue;
		// if there are no interactions, get out!
		if( !backEnd.vLight->localInteractions && !backEnd.vLight->globalInteractions && !backEnd.vLight->translucentInteractions )
			continue;

		if( r_useDepthBoundsTest.GetBool() ) {
			GL_DepthBoundsTest( backEnd.vLight->scissorRect.zmin, backEnd.vLight->scissorRect.zmax );
		}

		GL4_DrawLight_Stencil();

		// translucent surfaces never get stencil shadowed
		if( r_skipTranslucent.GetBool() )
			continue;
		qglStencilFunc( GL_ALWAYS, 128, 255 );
		backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
		GL4_RenderInteractions( backEnd.vLight->translucentInteractions );
		backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
	}
	if( r_useDepthBoundsTest.GetBool() ) {
		GL_DepthBoundsTest( 0, 0 );
	}
	// disable stencil shadow test
	qglStencilFunc( GL_ALWAYS, 128, 255 );
	GL_SelectTexture( 0 );

	/*for( int i = 0; i < 16; ++i )
		qglDisableVertexAttribArray( i );*/
	openGL4Renderer.EnableVertexAttribs( { VA_POSITION } );
}