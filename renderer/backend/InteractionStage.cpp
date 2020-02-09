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

struct InteractionShaderParams {
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
	uint64_t normalTexture;
	uint64_t diffuseTexture;
	uint64_t specularTexture;
	uint64_t lightProjectionTexture;
	uint64_t lightFalloffTexture;
	uint64_t padding;
};


InteractionStage::InteractionStage( ShaderParamsBuffer *shaderParamsBuffer )
	: shaderParamsBuffer( shaderParamsBuffer ), interactionShader( nullptr )
{}

void InteractionStage::Init() {}

void InteractionStage::Shutdown() {}

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

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );
	backEnd.currentSpace = NULL; // ambient/interaction shaders conflict

	// bind the vertex and fragment program
	ChooseInteractionProgram( vLight );
	Uniforms::Interaction *interactionUniforms = interactionShader->GetUniformGroup<Uniforms::Interaction>();
	interactionUniforms->RGTC.Set( globalImages->image_useNormalCompression.GetInteger() == 2 ? 1 : 0 );
	interactionUniforms->SetForShadows( interactionSurfs == vLight->translucentInteractions );

	// texture 1 will be the light falloff texture
	GL_SelectTexture( 1 );
	vLight->falloffImage->Bind();

	const idMaterial	*lightShader = vLight->lightShader;
	const float			*lightRegs = vLight->shaderRegisters;
	for ( int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++ ) {
		const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

		// ignore stages that fail the condition
		if ( !lightRegs[lightStage->conditionRegister] ) {
			continue;
		}

		// texture 2 will be the light projection texture
		GL_SelectTexture( 2 );
		lightStage->texture.image->Bind();

		for ( const drawSurf_t *surf = interactionSurfs; surf; surf = surf->nextOnLight ) {
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

	GL_SelectTexture( 0 );

	GLSLProgram::Deactivate();
}

void InteractionStage::ChooseInteractionProgram( viewLight_t *vLight ) {
	if ( vLight->lightShader->IsAmbientLight() ) {
		interactionShader = programManager->ambientInteractionShader;
	} else if ( vLight->shadowMapIndex ) {
		interactionShader = programManager->shadowMapInteractionShader;
	} else {
		interactionShader = programManager->stencilInteractionShader;
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

	// change the matrix and light projection vectors if needed
	if ( surf->space != backEnd.currentSpace ) {
		backEnd.currentSpace = surf->space;

		if( r_uniformTransforms.GetBool() && GLSLProgram::GetCurrentProgram() != nullptr ) {
			Uniforms::Global *transformUniforms = GLSLProgram::GetCurrentProgram()->GetUniformGroup<Uniforms::Global>();
			transformUniforms->Set( surf->space );
		} else
			qglLoadMatrixf( surf->space->modelViewMatrix );
	}

	// change the scissor if needed
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		GL_Scissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
		            backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
		            backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
		            backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		GL_CheckErrors();
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
	if ( !din->bumpImage && !r_skipBump.GetBool() )
		return;

	if ( !din->diffuseImage || r_skipDiffuse.GetBool() ) {
		din->diffuseImage = globalImages->blackImage;
	}

	if ( !din->specularImage || r_skipSpecular.GetBool() ) {
		din->specularImage = globalImages->blackImage;
	}

	// load all the shader parameters
	Uniforms::Interaction *interactionUniforms = interactionShader->GetUniformGroup<Uniforms::Interaction>();
	interactionUniforms->SetForInteraction( din );

	// set the textures
	// texture 0 will be the per-surface bump map
	if ( !din->bumpImage && interactionUniforms->hasTextureDNS.IsPresent() ) {
		interactionUniforms->hasTextureDNS.Set( 1, 0, 1 );
	} else {
		if( !din->bumpImage ) // FIXME Uh-oh! This should not happen
			return;
		GL_SelectTexture( 0 );
		din->bumpImage->Bind();
		if ( interactionUniforms->hasTextureDNS.IsPresent() ) {
			interactionUniforms->hasTextureDNS.Set( 1, 1, 1 );
		}
	}

	// texture 3 is the per-surface diffuse map
	GL_SelectTexture( 3 );
	din->diffuseImage->Bind();

	// texture 4 is the per-surface specular map
	GL_SelectTexture( 4 );
	din->specularImage->Bind();

	if ( r_softShadowsQuality.GetBool() && !backEnd.viewDef->IsLightGem() || backEnd.vLight->shadows == LS_MAPS )
		FB_BindShadowTexture();

	// draw it
	RB_DrawElementsWithCounters( din->surf );
}
