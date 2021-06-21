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
#include "DepthStage.h"
#include "RenderBackend.h"
#include "../FrameBuffer.h"
#include "../glsl.h"
#include "../FrameBufferManager.h"
#include "../GLSLProgramManager.h"


namespace {
	struct DepthUniforms : GLSLUniformGroup {
		UNIFORM_GROUP_DEF( DepthUniforms )

		DEFINE_UNIFORM( vec4, clipPlane )
		DEFINE_UNIFORM( mat4, inverseView )
		DEFINE_UNIFORM( sampler, texture )
	};

	void LoadShader( GLSLProgram *shader, int maxSupportedDrawsPerBatch, bool bindless ) {
		idHashMapDict defines;
		defines.Set( "MAX_SHADER_PARAMS", idStr::Fmt( "%d", maxSupportedDrawsPerBatch ) );
		if (bindless) {
			defines.Set( "BINDLESS_TEXTURES", "1" );
		}
		shader->InitFromFiles( "stages/depth/depth.vert.glsl", "stages/depth/depth.frag.glsl", defines );
		if (!bindless) {
			DepthUniforms *uniforms = shader->GetUniformGroup<DepthUniforms>();
			uniforms->texture.Set( 0 );
		}
		shader->BindUniformBlockLocation( 0, "ViewParamsBlock" );
		shader->BindUniformBlockLocation( 1, "ShaderParamsBlock" );
	}

	void CalcScissorParam( uint32_t scissor[4], const idScreenRect &screenRect ) {
		if ( !r_useScissor.GetBool() ) {
			scissor[0] = scissor[1] = 0;
			scissor[2] = frameBuffers->activeFbo->Width();
			scissor[3] = frameBuffers->activeFbo->Height();
			return;
		}

		float xScale = static_cast<float>(frameBuffers->activeFbo->Width()) / glConfig.vidWidth;
		float yScale = static_cast<float>(frameBuffers->activeFbo->Height()) / glConfig.vidHeight;

		scissor[0] = xScale * (backEnd.viewDef->viewport.x1 + screenRect.x1);
		scissor[1] = yScale * (backEnd.viewDef->viewport.y1 + screenRect.y1);
		scissor[2] = xScale * (screenRect.x2 + 1 - screenRect.x1);
		scissor[3] = yScale * (screenRect.y2 + 1 - screenRect.y1);
	}
}

struct DepthStage::ShaderParams {
	idMat4 modelViewMatrix;
	idMat4 textureMatrix;
	idVec4 color;
	uint32_t scissor[4];
	uint64_t textureHandle;
	float alphaTest;
	float padding;
};

DepthStage::DepthStage( DrawBatchExecutor* drawBatchExecutor )
	: drawBatchExecutor(drawBatchExecutor)
{
}

void DepthStage::Init() {
	uint maxShaderParamsArraySize = drawBatchExecutor->MaxShaderParamsArraySize<ShaderParams>();
	depthShader = programManager->LoadFromGenerator( "depth", [=](GLSLProgram *program) { LoadShader(program, maxShaderParamsArraySize, false); } );
	if( GLAD_GL_ARB_bindless_texture ) {
		depthShaderBindless = programManager->LoadFromGenerator( "depth_bindless", [=](GLSLProgram *program) { LoadShader(program, maxShaderParamsArraySize, true); } );
	}

	idHashMapDict fastDefines;
	fastDefines.Set( "MAX_SHADER_PARAMS", idStr::Fmt( "%d", drawBatchExecutor->MaxShaderParamsArraySize<idMat4>() ) );
	fastDepthShader = programManager->LoadFromFiles( "depth_fast", "stages/depth/depth_fast.vert.glsl", "stages/depth/depth_fast.frag.glsl", fastDefines );
}

void DepthStage::Shutdown() {}

void DepthStage::DrawDepth( const viewDef_t *viewDef, drawSurf_t **drawSurfs, int numDrawSurfs ) {
	if ( numDrawSurfs == 0 ) {
		return;
	}

	TRACE_GL_SCOPE( "DepthStage" );

	idList<drawSurf_t *> subviewSurfs;
	idList<drawSurf_t *> opaqueSurfs;
	idList<drawSurf_t *> remainingSurfs;
	PartitionSurfaces( drawSurfs, numDrawSurfs, subviewSurfs, opaqueSurfs, remainingSurfs );

	GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS );
	GenericDepthPass( subviewSurfs, viewDef );

	GL_State( GLS_DEPTHFUNC_LESS );
	FastDepthPass( opaqueSurfs, viewDef );
	GenericDepthPass( remainingSurfs, viewDef );

	// Make the early depth pass available to shaders. #3877
	if ( !viewDef->IsLightGem() && !r_skipDepthCapture.GetBool() ) {
		frameBuffers->UpdateCurrentDepthCopy();
	}
}

void DepthStage::PartitionSurfaces( drawSurf_t **drawSurfs, int numDrawSurfs, idList<drawSurf_t *> &subviewSurfs,
		idList<drawSurf_t *> &opaqueSurfs, idList<drawSurf_t *> &remainingSurfs ) {
	for ( int i = 0; i < numDrawSurfs; ++i ) {
		drawSurf_t *surf = drawSurfs[i];
		const idMaterial *material = surf->material;

		if ( !ShouldDrawSurf( surf ) ) {
			continue;
		}

		if ( material->GetSort() == SS_SUBVIEW ) {
			// sub view surfaces need to be rendered first in a generic pass due to mirror plane clipping
			subviewSurfs.AddGrow( surf );
		} else if ( material->Coverage() == MC_OPAQUE && !surf->space->weaponDepthHack ) {
			opaqueSurfs.AddGrow( surf );
		} else {
			const float *regs = surf->shaderRegisters;
			bool isOpaque = true;
			for ( int i = 0; i < material->GetNumStages(); ++i ) {
				const shaderStage_t *stage = material->GetStage( i );
				if ( !stage->hasAlphaTest ) {
					continue;
				}

				// check the stage enable condition
				if ( regs[stage->conditionRegister] == 0 ) {
					continue;
				}

				isOpaque = false;
				break;
			}

			if (isOpaque && !surf->space->weaponDepthHack) {
				opaqueSurfs.AddGrow( surf );
			} else {
				// these need alpha checks in the shader and thus can't be handled by the fast pass
				remainingSurfs.AddGrow( surf );
			}
		}
	}	
}

void DepthStage::GenericDepthPass( const idList<drawSurf_t *> drawSurfs, const viewDef_t *viewDef ) {
	if ( drawSurfs.Num() == 0 ) {
		return;
	}

	GLSLProgram *shader = renderBackend->ShouldUseBindlessTextures() ? depthShaderBindless : depthShader;
	shader->Activate();
	DepthUniforms *depthUniforms = shader->GetUniformGroup<DepthUniforms>();

	idMat4 inverseView;
	memcpy( inverseView.ToFloatPtr(), viewDef->worldSpace.modelViewMatrix, sizeof( inverseView ) );
	inverseView.InverseSelf();
	depthUniforms->inverseView.Set( inverseView );

	// pass mirror clip plane details to vertex shader if needed
	if ( viewDef->clipPlane) {
		depthUniforms->clipPlane.Set( *viewDef->clipPlane );
	} else {
		depthUniforms->clipPlane.Set( colorBlack );
	}

	// the first texture will be used for alpha tested surfaces
	GL_SelectTexture( 0 );
	depthUniforms->texture.Set( 0 );

	// decal surfaces may enable polygon offset
	qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	GL_State( GLS_DEPTHFUNC_LESS );

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	BeginDrawBatch();

	const drawSurf_t *curBatchCaches = drawSurfs[0];
	for ( const drawSurf_t *drawSurf : drawSurfs ) {
		if ( drawSurf->ambientCache.isStatic != curBatchCaches->ambientCache.isStatic || drawSurf->indexCache.isStatic != curBatchCaches->indexCache.isStatic ) {
			ExecuteDrawCalls();
		}
		curBatchCaches = drawSurf;
		DrawSurf( drawSurf );
	}
	ExecuteDrawCalls();
}

void DepthStage::FastDepthPass( const idList<drawSurf_t *> drawSurfs, const viewDef_t *viewDef ) {
	if ( drawSurfs.Num() == 0 ) {
		return;
	}

	if ( r_ignore.GetBool() ) {
		GenericDepthPass( drawSurfs, viewDef );
		return;
	}

	fastDepthShader->Activate();

	GL_State( GLS_DEPTHFUNC_LESS );

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	DrawBatch<idMat4> drawBatch = drawBatchExecutor->BeginBatch<idMat4>();
	uint batchIdx = 0;

	const drawSurf_t *curBatchCaches = drawSurfs[0];
	for ( const drawSurf_t *surf : drawSurfs ) {
		if ( batchIdx >= drawBatch.maxBatchSize
				|| surf->ambientCache.isStatic != curBatchCaches->ambientCache.isStatic
				|| surf->indexCache.isStatic != curBatchCaches->indexCache.isStatic ) {
			drawBatchExecutor->ExecuteDrawVertBatch( batchIdx );
			batchIdx = 0;
			drawBatch = drawBatchExecutor->BeginBatch<idMat4>();
		}

		memcpy( drawBatch.shaderParams[batchIdx].ToFloatPtr(), surf->space->modelViewMatrix, sizeof(idMat4) );
		drawBatch.surfs[batchIdx] = surf;
		++batchIdx;
		curBatchCaches = surf;
	}

	drawBatchExecutor->ExecuteDrawVertBatch( batchIdx );
}

bool DepthStage::ShouldDrawSurf(const drawSurf_t *surf) const {
    const idMaterial *shader = surf->material;

    if ( !shader->IsDrawn() ) {
        return false;
    }

    // some deforms may disable themselves by setting numIndexes = 0
    if ( !surf->numIndexes ) {
        return false;
    }

    // translucent surfaces don't put anything in the depth buffer and don't
    // test against it, which makes them fail the mirror clip plane operation
    if ( shader->Coverage() == MC_TRANSLUCENT ) {
        return false;
    }

    if ( !surf->ambientCache.IsValid() || !surf->indexCache.IsValid() ) {
#ifdef _DEBUG
        common->Printf( "DepthStage: missing vertex or index cache\n" );
#endif
        return false;
    }

    if ( surf->material->GetSort() == SS_PORTAL_SKY && g_enablePortalSky.GetInteger() == 2 ) {
        return false;
    }

    // get the expressions for conditionals / color / texcoords
    const float *regs = surf->shaderRegisters;

    // if all stages of a material have been conditioned off, don't do anything
    int stage;
    for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {
        const shaderStage_t *pStage = shader->GetStage( stage );
        // check the stage enable condition
        if ( regs[ pStage->conditionRegister ] != 0 ) {
            break;
        }
    }
    return stage != shader->GetNumStages();
}

void DepthStage::DrawSurf( const drawSurf_t *surf ) {
	if ( surf->space->weaponDepthHack ) {
		// this is a state change, need to finish any previous calls
		ExecuteDrawCalls();
		RB_EnterWeaponDepthHack();
	}

	const idMaterial *shader = surf->material;

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		// this is a state change, need to finish any previous calls
		ExecuteDrawCalls();
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	CreateDrawCommands( surf );

	// reset polygon offset
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		ExecuteDrawCalls();
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( surf->space->weaponDepthHack ) {
		ExecuteDrawCalls();
		RB_LeaveDepthHack();
	}
}

void DepthStage::CreateDrawCommands( const drawSurf_t *surf ) {
	const idMaterial		*shader = surf->material;
	const float				*regs = surf->shaderRegisters;

	bool drawSolid = false;

	if ( shader->Coverage() == MC_OPAQUE ) {
		drawSolid = true;
	}

	// we may have multiple alpha tested stages
	if ( shader->Coverage() == MC_PERFORATED ) {
		// if the only alpha tested stages are condition register omitted,
		// draw a normal opaque surface
		bool	didDraw = false;

		GL_CheckErrors();

		// perforated surfaces may have multiple alpha tested stages
		for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {
			const shaderStage_t *pStage = shader->GetStage( stage );

			if ( !pStage->hasAlphaTest ) {
				continue;
			}

			// check the stage enable condition
			if ( regs[pStage->conditionRegister] == 0 ) {
				continue;
			}

			// if we at least tried to draw an alpha tested stage,
			// we won't draw the opaque surface
			didDraw = true;

			// skip the entire stage if alpha would be black
			if ( regs[pStage->color.registers[3]] <= 0 ) {
				continue;
			}
			IssueDrawCommand( surf, pStage );
		}

		if ( !didDraw ) {
			drawSolid = true;
		}
	}

	if ( drawSolid ) {  // draw the entire surface solid
		IssueDrawCommand( surf, nullptr );
	}
}

void DepthStage::IssueDrawCommand( const drawSurf_t *surf, const shaderStage_t *stage ) {
	if( stage && !renderBackend->ShouldUseBindlessTextures() && !stage->texture.image->IsBound( 0 ) ) {
		ExecuteDrawCalls();
		stage->texture.image->Bind();
	}

	ShaderParams &params = drawBatch.shaderParams[currentIndex];

	memcpy( params.modelViewMatrix.ToFloatPtr(), surf->space->modelViewMatrix, sizeof(idMat4) );
	CalcScissorParam( params.scissor, surf->scissorRect );
	params.alphaTest = -1.f;

	if ( surf->material->GetSort() == SS_SUBVIEW ) {
		// subviews will just down-modulate the color buffer by overbright
		params.color[0] = params.color[1] = params.color[2] = 1.0f / backEnd.overBright;
		params.color[3] = 1;
	} else {
		// others just draw black
		params.color = idVec4(0, 0, 0, 1);
	}

	if( stage ) {
		// set the alpha modulate
		params.color[3] = surf->shaderRegisters[stage->color.registers[3]];
		params.alphaTest = surf->shaderRegisters[stage->alphaTestRegister];

		if( renderBackend->ShouldUseBindlessTextures() ) {
			stage->texture.image->MakeResident();
			params.textureHandle = stage->texture.image->BindlessHandle();
		}

		if( stage->texture.hasMatrix ) {
			RB_GetShaderTextureMatrix( surf->shaderRegisters, &stage->texture, params.textureMatrix.ToFloatPtr() );
		} else {
			params.textureMatrix.Identity();
		}
	}

	drawBatch.surfs[currentIndex] = surf;
	++currentIndex;
	if ( currentIndex == drawBatch.maxBatchSize ) {
		ExecuteDrawCalls();
	}
}

void DepthStage::BeginDrawBatch() {
	currentIndex = 0;
	drawBatch = drawBatchExecutor->BeginBatch<ShaderParams>();
}

void DepthStage::ExecuteDrawCalls() {
	if (currentIndex == 0) {
		return;
	}

	drawBatchExecutor->ExecuteDrawVertBatch(currentIndex);
	BeginDrawBatch();
}
