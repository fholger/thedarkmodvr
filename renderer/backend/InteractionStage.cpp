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

#include "InteractionStage.h"
#include "../Profiling.h"
#include "../glsl.h"
#include "../GLSLProgramManager.h"
#include "../FrameBuffer.h"
#include "ShaderParamsBuffer.h"
#include "../AmbientOcclusionStage.h"

// NOTE: must match struct in shader, beware of std140 layout requirements and alignment!
struct InteractionStage::ShaderParams {
	idMat4 modelMatrix;
	idMat4 modelViewMatrix;
	idVec4 bumpMatrix[2];
	idVec4 diffuseMatrix[2];
	idVec4 specularMatrix[2];
	idMat4 lightProjectionFalloff;
	idVec4 colorModulate;
	idVec4 colorAdd;
	idVec4 lightOrigin;
	idVec4 viewOrigin;
	idVec4 diffuseColor;
	idVec4 specularColor;
	idVec4 hasTextureDNS;
	idVec4 ambientRimColor;
};

struct InteractionStage::DrawCall {
	const drawSurf_t *surf;
	idImage *diffuseTexture;
	idImage *specularTexture;
	idImage *bumpTexture;
};

namespace {
	struct InteractionUniforms: GLSLUniformGroup {
		UNIFORM_GROUP_DEF( InteractionUniforms )

		DEFINE_UNIFORM( int, idx )

		DEFINE_UNIFORM( float, cubic )
		DEFINE_UNIFORM( sampler, normalTexture )
		DEFINE_UNIFORM( sampler, diffuseTexture )
		DEFINE_UNIFORM( sampler, specularTexture )
		DEFINE_UNIFORM( sampler, lightProjectionTexture )
		DEFINE_UNIFORM( sampler, lightProjectionCubemap )
		DEFINE_UNIFORM( sampler, lightFalloffTexture )
		DEFINE_UNIFORM( sampler, lightFalloffCubemap )

		DEFINE_UNIFORM( float, minLevel )
		DEFINE_UNIFORM( float, gamma )
		DEFINE_UNIFORM( vec4, rimColor )

		DEFINE_UNIFORM( float, advanced )
		DEFINE_UNIFORM( int, shadows )
		DEFINE_UNIFORM( int, shadowMapCullFront )
		DEFINE_UNIFORM( vec4, shadowRect )
		DEFINE_UNIFORM( int, softShadowsQuality )
		DEFINE_UNIFORM( vec2, softShadowsSamples )
		DEFINE_UNIFORM( float, softShadowsRadius )
		DEFINE_UNIFORM( int, shadowMap )
		DEFINE_UNIFORM( sampler, depthTexture )
		DEFINE_UNIFORM( sampler, stencilTexture )
		DEFINE_UNIFORM( vec2, renderResolution )

		DEFINE_UNIFORM( float, RGTC )

		// temp
		DEFINE_UNIFORM( int, shadowMapHistory )
		DEFINE_UNIFORM( int, frameCount )
		DEFINE_UNIFORM( vec3, lightSamples )
		DEFINE_UNIFORM( int, testSpecularFix )
		DEFINE_UNIFORM( int, testBumpmapLightTogglingFix )
		DEFINE_UNIFORM( int, testStencilSelfShadowFix )
	};

	const int MAX_DRAWS_PER_BATCH = 32;
}


InteractionStage::InteractionStage( ShaderParamsBuffer *shaderParamsBuffer )
	: shaderParamsBuffer( shaderParamsBuffer ), interactionShader( nullptr )
{}

void InteractionStage::Init() {
	ambientInteractionShader = programManager->Find( "interaction_ambient" );
	if( ambientInteractionShader == nullptr ) {
		ambientInteractionShader = programManager->LoadFromFiles( "interaction_ambient", "stages/interaction/interaction.ambient.vs.glsl", "stages/interaction/interaction.ambient.fs.glsl" );
	}
	stencilInteractionShader = programManager->Find( "interaction_stencil" );
	if( stencilInteractionShader == nullptr ) {
		stencilInteractionShader = programManager->LoadFromFiles( "interaction_stencil", "stages/interaction/interaction.stencil.vs.glsl", "stages/interaction/interaction.stencil.fs.glsl" );
	}

	drawCalls = new DrawCall[MAX_DRAWS_PER_BATCH];
}

void InteractionStage::Shutdown() {
	delete[] drawCalls;
}

idCVar r_sortByMaterial("r_sortByMaterial", "1", CVAR_RENDERER|CVAR_BOOL, "Sort draw surfs by material");

void InteractionStage::DrawInteractions( viewLight_t *vLight, const drawSurf_t *interactionSurfs ) {
	if ( !interactionSurfs ) {
		return;
	}
	if ( vLight->lightShader->IsAmbientLight() ) {
		if ( r_skipAmbient.GetInteger() & 2 )
			return;
	} else if ( r_skipInteractions.GetBool() ) 
		return;

	GL_PROFILE( "DrawInteractions" );

	// if using float buffers, alpha values are not clamped and can stack up quite high, since most interactions add 1 to its value
	// this in turn causes issues with some shader stage materials that use DST_ALPHA blending.
	// masking the alpha channel for interactions seems to fix those issues, but only do it for float buffers in case it has
	// unwanted side effects
	int alphaMask = r_fboColorBits.GetInteger() == 64 ? GLS_ALPHAMASK : 0;

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | alphaMask | backEnd.depthFunc );
	backEnd.currentSpace = NULL; // ambient/interaction shaders conflict
	ResetShaderParams();

	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( vLight->scissorRect ) ) {
		backEnd.currentScissor = vLight->scissorRect;
		GL_ScissorVidSize( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
		            backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
		            backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
		            backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		GL_CheckErrors();
	}

	// bind the vertex and fragment program
	ChooseInteractionProgram( vLight );
	Uniforms::Interaction *interactionUniforms = interactionShader->GetUniformGroup<Uniforms::Interaction>();
	interactionUniforms->RGTC.Set( globalImages->image_useNormalCompression.GetInteger() == 2 ? 1 : 0 );
	interactionUniforms->SetForShadows( interactionSurfs == vLight->translucentInteractions );
	interactionUniforms->lightProjectionCubemap.Set( 3 );
	interactionUniforms->lightProjectionTexture.Set( 3 );
	interactionUniforms->lightFalloffCubemap.Set( 2 );
	interactionUniforms->lightFalloffTexture.Set( 2 );
	interactionUniforms->normalTexture.Set( 4 );
	interactionUniforms->diffuseTexture.Set( 5 );
	interactionUniforms->specularTexture.Set( 6 );
	interactionUniforms->ssaoTexture.Set( 7 );
	if( backEnd.vLight->lightShader->IsAmbientLight() && ambientOcclusion->ShouldEnableForCurrentView() ) {
		ambientOcclusion->BindSSAOTexture( 7 );
	}

	std::vector<const drawSurf_t*> drawSurfs;
	for ( const drawSurf_t *surf = interactionSurfs; surf; surf = surf->nextOnLight) {
		drawSurfs.push_back( surf );
	}
	if (r_sortByMaterial.GetBool()) {
		std::sort( drawSurfs.begin(), drawSurfs.end(), [](const drawSurf_t *a, const drawSurf_t *b) {
			return a->material < b->material;
		} );
	}

	GL_SelectTexture( 2 );
	vLight->falloffImage->Bind();

	if ( r_softShadowsQuality.GetBool() && !backEnd.viewDef->IsLightGem() || vLight->shadows == LS_MAPS )
		BindShadowTexture();

	const idMaterial	*lightShader = vLight->lightShader;
	const float			*lightRegs = vLight->shaderRegisters;
	for ( int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++ ) {
		const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

		// ignore stages that fail the condition
		if ( !lightRegs[lightStage->conditionRegister] ) {
			continue;
		}

		GL_SelectTexture( 3 );
		lightStage->texture.image->Bind();

		for ( const drawSurf_t *surf : drawSurfs ) {
			if ( surf->dsFlags & DSF_SHADOW_MAP_ONLY ) {
				continue;
			}
			if ( backEnd.currentSpace != surf->space ) {
				// FIXME needs a better integration with RB_CreateSingleDrawInteractions
				interactionUniforms->modelMatrix.Set( surf->space->modelMatrix );
			}

			// set the vertex pointers
			vertexCache.VertexPosition( surf->ambientCache );

			ProcessSingleSurface( vLight, lightStage, surf );
		}
	}

	ExecuteDrawCalls();

	GL_SelectTexture( 0 );

	GLSLProgram::Deactivate();
}

void InteractionStage::BindShadowTexture() {
	if ( backEnd.vLight->shadowMapIndex ) {
		GL_SelectTexture( 6 );
		globalImages->shadowAtlas->Bind();
	} else {
		GL_SelectTexture( 6 );
		globalImages->currentDepthImage->Bind();
		GL_SelectTexture( 7 );

		globalImages->shadowDepthFbo->Bind();
		qglTexParameteri( GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_STENCIL_INDEX );
	}
}

void InteractionStage::ChooseInteractionProgram( viewLight_t *vLight ) {
	if ( vLight->lightShader->IsAmbientLight() ) {
		interactionShader = ambientInteractionShader;
		Uniforms::Interaction *uniforms = ambientInteractionShader->GetUniformGroup<Uniforms::Interaction>();
		uniforms->ambient = true;
	} else if ( vLight->shadowMapIndex ) {
		// FIXME: port shadowmap shader
		interactionShader = stencilInteractionShader;
	} else {
		interactionShader = stencilInteractionShader;
	}
	interactionShader->Activate();
}

void InteractionStage::ProcessSingleSurface( viewLight_t *vLight, const shaderStage_t *lightStage, const drawSurf_t *surf ) {
	const idMaterial	*material = surf->material;
	const float			*surfaceRegs = surf->shaderRegisters;
	const idMaterial	*lightShader = vLight->lightShader;
	const float			*lightRegs = vLight->shaderRegisters;
	drawInteraction_t	inter;

	if ( !surf->ambientCache.IsValid() ) {
		return;
	}

	if ( lightShader->IsAmbientLight() ) {
		inter.worldUpLocal.x = surf->space->modelMatrix[2];
		inter.worldUpLocal.y = surf->space->modelMatrix[6];
		inter.worldUpLocal.z = surf->space->modelMatrix[10];
		auto ambientRegs = material->GetAmbientRimColor().registers;
		if ( ambientRegs[0] ) {
			for ( int i = 0; i < 3; i++ )
				inter.ambientRimColor[i] = surfaceRegs[ambientRegs[i]];
			inter.ambientRimColor[3] = 1;
		} else
			inter.ambientRimColor.Zero();
	}

	inter.surf = surf;

	R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
	inter.localLightOrigin[3] = 0;
	inter.localViewOrigin[3] = 1;
	inter.cubicLight = lightShader->IsCubicLight(); // nbohr1more #3881: cubemap lights
	inter.ambientLight = lightShader->IsAmbientLight();

	// the base projections may be modified by texture matrix on light stages
	idPlane lightProject[4];
	R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[0], lightProject[0] );
	R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[1], lightProject[1] );
	R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[2], lightProject[2] );
	R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[3], lightProject[3] );

	memcpy( inter.lightProjection, lightProject, sizeof( inter.lightProjection ) );

	// now multiply the texgen by the light texture matrix
	if ( lightStage->texture.hasMatrix ) {
		RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, backEnd.lightTextureMatrix );
		void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float *textureMatrix );
		RB_BakeTextureMatrixIntoTexgen( reinterpret_cast<class idPlane *>(inter.lightProjection), backEnd.lightTextureMatrix );
	}
	inter.bumpImage = NULL;
	inter.specularImage = NULL;
	inter.diffuseImage = NULL;
	inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
	inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

	// backEnd.lightScale is calculated so that lightColor[] will never exceed
	// tr.backEndRendererMaxLight
	float lightColor[4] = {
		lightColor[0] = backEnd.lightScale * lightRegs[lightStage->color.registers[0]],
		lightColor[1] = backEnd.lightScale * lightRegs[lightStage->color.registers[1]],
		lightColor[2] = backEnd.lightScale * lightRegs[lightStage->color.registers[2]],
		lightColor[3] = lightRegs[lightStage->color.registers[3]]
	};

	// go through the individual stages
	for ( int surfaceStageNum = 0; surfaceStageNum < material->GetNumStages(); surfaceStageNum++ ) {
		const shaderStage_t	*surfaceStage = material->GetStage( surfaceStageNum );

		if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) // ignore stage that fails the condition
			continue;


		void R_SetDrawInteraction( const shaderStage_t *surfaceStage, const float *surfaceRegs, idImage **image, idVec4 matrix[2], float color[4] );

		switch ( surfaceStage->lighting ) {
		case SL_AMBIENT: {
			// ignore ambient stages while drawing interactions
			break;
		}
		case SL_BUMP: {				
			if ( !r_skipBump.GetBool() ) {
				PrepareDrawCommand( &inter ); // draw any previous interaction
				inter.diffuseImage = NULL;
				inter.specularImage = NULL;
				R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL );
			}
			break;
		}
		case SL_DIFFUSE: {
			if ( inter.diffuseImage ) {
				PrepareDrawCommand( &inter );
			}
			R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage,
								  inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
			inter.diffuseColor[0] *= lightColor[0];
			inter.diffuseColor[1] *= lightColor[1];
			inter.diffuseColor[2] *= lightColor[2];
			inter.diffuseColor[3] *= lightColor[3];
			inter.vertexColor = surfaceStage->vertexColor;
			break;
		}
		case SL_SPECULAR: {
			// nbohr1more: #4292 nospecular and nodiffuse fix
			if ( vLight->noSpecular ) {
				break;
			}
			if ( inter.specularImage ) {
				PrepareDrawCommand( &inter );
			}
			R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage,
								  inter.specularMatrix, inter.specularColor.ToFloatPtr() );
			inter.specularColor[0] *= lightColor[0];
			inter.specularColor[1] *= lightColor[1];
			inter.specularColor[2] *= lightColor[2];
			inter.specularColor[3] *= lightColor[3];
			inter.vertexColor = surfaceStage->vertexColor;
			break;
		}
		}
	}

	// draw the final interaction
	PrepareDrawCommand( &inter );
}

void InteractionStage::PrepareDrawCommand( drawInteraction_t *din ) {
	DrawCall &drawCall = drawCalls[currentIndex];
	ShaderParams &params = shaderParams[currentIndex];

	if ( !din->bumpImage && !r_skipBump.GetBool() )
		return;

	if ( !din->diffuseImage || r_skipDiffuse.GetBool() ) {
		din->diffuseImage = globalImages->blackImage;
	}

	if ( !din->specularImage || r_skipSpecular.GetBool() ) {
		din->specularImage = globalImages->blackImage;
	}

	memcpy( params.modelMatrix.ToFloatPtr(), din->surf->space->modelMatrix, sizeof(idMat4) );
	memcpy( params.modelViewMatrix.ToFloatPtr(), din->surf->space->modelViewMatrix, sizeof(idMat4) );
	params.bumpMatrix[0] = din->bumpMatrix[0];
	params.bumpMatrix[1] = din->bumpMatrix[1];
	params.diffuseMatrix[0] = din->diffuseMatrix[0];
	params.diffuseMatrix[1] = din->diffuseMatrix[1];
	params.specularMatrix[0] = din->specularMatrix[0];
	params.specularMatrix[1] = din->specularMatrix[1];
	memcpy( params.lightProjectionFalloff.ToFloatPtr(), din->lightProjection, sizeof(idMat4) );
	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		params.colorModulate = idVec4(0, 0, 0, 0);
		params.colorAdd = idVec4(1, 1, 1, 1);
		break;
	case SVC_MODULATE:
		params.colorModulate = idVec4(1, 1, 1, 1);
		params.colorAdd = idVec4(0, 0, 0, 0);
		break;
	case SVC_INVERSE_MODULATE:
		params.colorModulate = idVec4(-1, -1, -1, -1);
		params.colorAdd = idVec4(1, 1, 1, 1);
		break;
	}
	params.lightOrigin = din->ambientLight ? din->worldUpLocal : din->localLightOrigin;
	params.viewOrigin = din->localViewOrigin;
	params.diffuseColor = din->diffuseColor;
	params.specularColor = din->specularColor;
	if ( !din->bumpImage ) {
		params.hasTextureDNS = idVec4(1, 0, 1, 0);
	} else {
		params.hasTextureDNS = idVec4(1, 1, 1, 0);
	}
	params.ambientRimColor = din->ambientRimColor;

	drawCall.surf = din->surf;
	drawCall.diffuseTexture = din->diffuseImage;
	drawCall.specularTexture = din->specularImage;
	drawCall.bumpTexture = din->bumpImage;

	++currentIndex;
	if (currentIndex == MAX_DRAWS_PER_BATCH) {
		ExecuteDrawCalls();
		ResetShaderParams();
	}
}

int InteractionStage::FindBoundTextureUnit( idImage *texture, int usedUnits ) {
	for( int i = 0; i < usedUnits; ++i ) {
		if( backEnd.glState.tmu[i].current2DMap == texture->texnum || backEnd.glState.tmu[i].currentCubeMap == texture->texnum) {
			return i;
		}
	}
	return -1;
}

void InteractionStage::ResetShaderParams() {
	currentIndex = 0;
	currentTextureUnit = 4;
	shaderParams = shaderParamsBuffer->Request<ShaderParams>(MAX_DRAWS_PER_BATCH);
}

void InteractionStage::ExecuteDrawCalls() {
	int totalDrawCalls = currentIndex;
	if (totalDrawCalls == 0) {
		return;
	}

	shaderParamsBuffer->Commit( shaderParams, totalDrawCalls );
	shaderParamsBuffer->BindRange( 1, shaderParams, totalDrawCalls );

	for( int i = 0; i < currentIndex; ++i ) {
		GL_SelectTexture( 4 );
		drawCalls[i].bumpTexture->Bind();
		GL_SelectTexture( 5 );
		drawCalls[i].diffuseTexture->Bind();
		GL_SelectTexture( 6 );
		drawCalls[i].specularTexture->Bind();
		qglVertexAttribI1i( 15, i );
		vertexCache.VertexPosition( drawCalls[i].surf->ambientCache );
		RB_DrawElementsWithCounters( drawCalls[i].surf );		
	}
}
