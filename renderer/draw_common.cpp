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

#include "tr_local.h"
#include "glsl.h"
#include "FrameBuffer.h"
#include "Profiling.h"
#include "GLSLProgram.h"
#include "GLSLUniforms.h"
#include "GLSLProgramManager.h"
#include "AmbientOcclusionStage.h"
#include "FrameBufferManager.h"

struct CubemapUniforms : GLSLUniformGroup {
	UNIFORM_GROUP_DEF( CubemapUniforms );

	DEFINE_UNIFORM( float, reflective );
	DEFINE_UNIFORM( int, skybox );
	DEFINE_UNIFORM( vec3, viewOrigin );
	DEFINE_UNIFORM( mat4, modelMatrix );
};

struct BumpyEnvironmentUniforms : GLSLUniformGroup {
	UNIFORM_GROUP_DEF( BumpyEnvironmentUniforms );

	DEFINE_UNIFORM( vec4, colorAdd );
	DEFINE_UNIFORM( vec4, colorModulate );
};

struct FogUniforms : GLSLUniformGroup {
	UNIFORM_GROUP_DEF( FogUniforms );

	DEFINE_UNIFORM( vec4, tex0PlaneS );
	DEFINE_UNIFORM( vec4, tex1PlaneT );
	DEFINE_UNIFORM( vec3, fogColor );
	DEFINE_UNIFORM( float, fogEnter );
	DEFINE_UNIFORM( float, fogDensity );
	DEFINE_UNIFORM( float, fogAlpha );
	DEFINE_UNIFORM( int, newFog );
	DEFINE_UNIFORM( vec3, viewOrigin );
	DEFINE_UNIFORM( vec4, frustumPlanes );
};

struct BlendUniforms : GLSLUniformGroup {
	UNIFORM_GROUP_DEF( BlendUniforms );

	DEFINE_UNIFORM( vec4, tex0PlaneS );
	DEFINE_UNIFORM( vec4, tex0PlaneT );
	DEFINE_UNIFORM( vec4, tex0PlaneQ );
	DEFINE_UNIFORM( vec4, tex1PlaneS );
	DEFINE_UNIFORM( vec4, blendColor );
};

struct FrobUniforms : GLSLUniformGroup {
	UNIFORM_GROUP_DEF( FrobUniforms );

	DEFINE_UNIFORM( float, pulse );
};

/*
================
RB_PrepareStageTexturing_ReflectCube
Extracted from RB_PrepareStageTexturing
================
*/
ID_NOINLINE void RB_PrepareStageTexturing_ReflectCube( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	// see if there is also a bump map specified
	const shaderStage_t *bumpStage = surf->material->GetBumpStage();
	if ( bumpStage ) {
		// per-pixel reflection mapping with bump mapping
		GL_SelectTexture( 1 );
		bumpStage->texture.image->Bind();
		GL_SelectTexture( 0 );

		if ( r_useGLSL ) {
			programManager->bumpyEnvironment->Activate();
			programManager->bumpyEnvironment->GetUniformGroup<Uniforms::Global>()->Set( surf->space );
			BumpyEnvironmentUniforms *uniforms = programManager->bumpyEnvironment->GetUniformGroup<BumpyEnvironmentUniforms>();
			uniforms->colorAdd.Set(0, 0, 0, 0);
			uniforms->colorModulate.Set(0, 0, 0, 0);
		} else // Program env 5, 6, 7, 8 have been set in RB_SetProgramEnvironmentSpace
			R_UseProgramARB( VPROG_BUMPY_ENVIRONMENT );
	} else {
		if ( r_useGLSL ) {
			GLSLProgram *environmentShader = R_FindGLSLProgram( "environment" );
			environmentShader->Activate();
			environmentShader->GetUniformGroup<Uniforms::Global>()->Set( surf->space );
		}  else
			R_UseProgramARB( VPROG_ENVIRONMENT );
	}
}

/*
================
RB_PrepareStageTexturing
================
*/
void RB_PrepareStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	// set privatePolygonOffset if necessary
	if ( pStage->privatePolygonOffset ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
	}

	// set the texture matrix if needed
	RB_LoadShaderTextureMatrix( surf->shaderRegisters, pStage );

	// texgens
	switch ( pStage->texture.texgen ) {
	case TG_SCREEN:
		programManager->oldStageShader->GetUniformGroup<OldStageUniforms>()->screenTex.Set( 1 );
		break;
	case TG_REFLECT_CUBE:
		RB_PrepareStageTexturing_ReflectCube( pStage, surf );
		break;
	}
}

/*
================
RB_FinishStageTexturing
================
*/
void RB_FinishStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	// unset privatePolygonOffset if necessary
	if ( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}

	switch ( pStage->texture.texgen ) {
	case TG_SCREEN:
		programManager->oldStageShader->GetUniformGroup<OldStageUniforms>()->screenTex.Set( 0 );
		break;
	case TG_REFLECT_CUBE:
		const shaderStage_t *bumpStage = surf->material->GetBumpStage();
		if ( bumpStage ) {
			GL_SelectTexture( 0 );

			//if ( r_useGLSL )
			//	programManager->cubeMapShader->GetUniformGroup<CubemapUniforms>()->reflective.Set( 0 );
		}
		GLSLProgram::Deactivate();

		if (!r_useGLSL)
			R_UseProgramARB();

		break;
	}

	RB_LoadShaderTextureMatrix( NULL, pStage );
}

/*
=============================================================================================

FILL DEPTH BUFFER

=============================================================================================
*/


/*
==================
RB_T_FillDepthBuffer
==================
*/
void RB_T_FillDepthBuffer( const drawSurf_t *surf ) {
	int						stage;
	const idMaterial		*shader;
	const shaderStage_t		*pStage;
	const float				*regs;
	//float					color[4];

	shader = surf->material;

	if ( !shader->IsDrawn() ) {
		return;
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !surf->numIndexes ) {
		return;
	}

	// translucent surfaces don't put anything in the depth buffer and don't
	// test against it, which makes them fail the mirror clip plane operation
	if ( shader->Coverage() == MC_TRANSLUCENT ) {
		return;
	}

	if ( !surf->ambientCache.IsValid() ) {
		common->Printf( "RB_T_FillDepthBuffer: !tri->ambientCache\n" );
		return;
	}

	if ( surf->material->GetSort() == SS_PORTAL_SKY && g_enablePortalSky.GetInteger() == 2 ) {
		return;
	}

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	// if all stages of a material have been conditioned off, don't do anything
	for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {
		pStage = shader->GetStage( stage );
		// check the stage enable condition
		if ( regs[ pStage->conditionRegister ] != 0 ) {
			break;
		}
	}

	if ( stage == shader->GetNumStages() ) {
		return;
	}

	// set polygon offset if necessary
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	RB_SingleSurfaceToDepthBuffer( programManager->depthShader, surf );

	// reset polygon offset
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}

/*
=====================
RB_CopyDepthBuffer

Revelator: Create a depth copy of the entire map.
=====================
*//*
static void RB_CopyDepthBuffer( void ) {
	globalImages->currentDepthImage->CopyDepthBuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 -
		backEnd.viewDef->viewport.x1 + 1,
		backEnd.viewDef->viewport.y2 -
		backEnd.viewDef->viewport.y1 + 1, true);
}*/

/*
=====================
RB_STD_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void RB_STD_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	static idCVarBool r_skipDepthPass( "r_skipDepthPass", "0", CVAR_RENDERER, "" );
	if ( r_skipDepthPass )
		return;
	
	GL_PROFILE( "STD_FillDepthBuffer" );

	RB_LogComment( "---------- RB_STD_FillDepthBuffer ----------\n" );

	programManager->depthShader->Activate();
	Uniforms::Depth * depthUniforms = programManager->depthShader->GetUniformGroup<Uniforms::Depth>();
	depthUniforms->alphaTest.Set( -1 );	// no alpha test by default

	if ( backEnd.viewDef->clipPlane) {		// pass mirror clip plane details to vertex shader if needed
		idMat4 m;
		memcpy( m.ToFloatPtr(), backEnd.viewDef->worldSpace.modelViewMatrix, sizeof( m ) );
		m.InverseSelf();
		depthUniforms->matViewRev.Set( m );
		depthUniforms->clipPlane.Set( *backEnd.viewDef->clipPlane );
	} else {
		depthUniforms->clipPlane.Set( colorBlack );
	}

	// the first texture will be used for alpha tested surfaces
	GL_SelectTexture( 0 );

	// decal surfaces may enable polygon offset
	qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	GL_State( GLS_DEPTHFUNC_LESS );

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );

	RB_RenderDrawSurfListWithFunction( drawSurfs, numDrawSurfs, RB_T_FillDepthBuffer );

	RB_Multi_DrawElements();

	// Make the early depth pass available to shaders. #3877
	if ( !backEnd.viewDef->IsLightGem() && !r_skipDepthCapture.GetBool() ) {
		frameBuffers->UpdateCurrentDepthCopy();
		RB_SetProgramEnvironment();
	}
	GLSLProgram::Deactivate();
	GL_CheckErrors();
}

/*
=============================================================================================

SHADER PASSES

=============================================================================================
*/

/*
==================
RB_SetProgramEnvironment

Sets variables that can be used by all vertex programs

[SteveL #3877] Note on the use of fragment program environmental variables.
Parameters 0 and 1 are set here to allow conversion of screen coordinates to
texture coordinates, for use when sampling _currentRender.
Those same parameters 0 and 1, plus 2 and 3, are given entirely different
meanings in draw_arb2.cpp while light interactions are being drawn.
This function is called again before currentRender size is needed by post processing
effects are done, so there's no clash.
Only parameters 0..3 were in use before #3877. Now I've used a new parameter 4 for the
size of _currentDepth. It's needed throughout, including by light interactions, and its
size might in theory differ from _currentRender.
Parameters 5 and 6 are used by soft particles #3878. Note these can be freely reused by different draw calls.
==================
*/
void RB_SetProgramEnvironment( void ) {
	if (r_useGLSL)
		return;

	float	parm[4];
	int		pot;

	// screen power of two correction factor, assuming the copy to _currentRender
	// also copied an extra row and column for the bilerp
	int	 w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	pot = globalImages->currentRenderImage->uploadWidth;
	parm[0] = ( float )w / pot;

	int	 h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	pot = globalImages->currentRenderImage->uploadHeight;
	parm[1] = ( float )h / pot;

	parm[2] = 0;
	parm[3] = 1;
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 0, parm );
	qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, parm );

	// window coord to 0.0 to 1.0 conversion
	parm[0] = 1.0 / w;
	parm[1] = 1.0 / h;
	parm[2] = 0;
	parm[3] = 1;
	qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, parm );

	// #3877: Allow shaders to access depth buffer.
	// Two useful ratios are packed into this parm: [0] and [1] hold the x,y multipliers you need to map a screen
	// coordinate (fragment position) to the depth image: those are simply the reciprocal of the depth
	// image size, which has been rounded up to a power of two. Slots [3] and [4] hold the ratio of the depth image
	// size to the current render image size. These sizes can differ if the game crops the render viewport temporarily
	// during post-processing effects. The depth render is smaller during the effect too, but the depth image doesn't
	// need to be downsized, whereas the current render image does get downsized when it's captured by the game after
	// the skybox render pass. The ratio is needed to map between the two render images.
	parm[0] = 1.0f / globalImages->currentDepthImage->uploadWidth;
	parm[1] = 1.0f / globalImages->currentDepthImage->uploadHeight;
	parm[2] = static_cast<float>( globalImages->currentRenderImage->uploadWidth ) / globalImages->currentDepthImage->uploadWidth;
	parm[3] = static_cast<float>( globalImages->currentRenderImage->uploadHeight ) / globalImages->currentDepthImage->uploadHeight;
	qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 4, parm );

	//
	// set eye position in global space
	//
	parm[0] = backEnd.viewDef->renderView.vieworg[0];
	parm[1] = backEnd.viewDef->renderView.vieworg[1];
	parm[2] = backEnd.viewDef->renderView.vieworg[2];
	parm[3] = 1.0;
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 1, parm );


}

/*
==================
RB_SetProgramEnvironmentSpace

Sets variables related to the current space that can be used by all vertex programs
==================
*/
void RB_SetProgramEnvironmentSpace( void ) {
	if (r_useGLSL)
		return;

	const struct viewEntity_s *space = backEnd.currentSpace;
	float	parm[4];

	// set eye position in local space
	R_GlobalPointToLocal( space->modelMatrix, backEnd.viewDef->renderView.vieworg, *( idVec3 * )parm );
	parm[3] = 1.0;
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 5, parm );

	// we need the model matrix without it being combined with the view matrix
	// so we can transform local vectors to global coordinates
	parm[0] = space->modelMatrix[0];
	parm[1] = space->modelMatrix[4];
	parm[2] = space->modelMatrix[8];
	parm[3] = space->modelMatrix[12];
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 6, parm );
	parm[0] = space->modelMatrix[1];
	parm[1] = space->modelMatrix[5];
	parm[2] = space->modelMatrix[9];
	parm[3] = space->modelMatrix[13];
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 7, parm );
	parm[0] = space->modelMatrix[2];
	parm[1] = space->modelMatrix[6];
	parm[2] = space->modelMatrix[10];
	parm[3] = space->modelMatrix[14];
	qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 8, parm );
}

/*
==================
RB_STD_T_RenderShaderPasses_OldStage

Extracted from the giantic loop in RB_STD_T_RenderShaderPasses
==================
*/
void RB_STD_T_RenderShaderPasses_OldStage( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetInteger() & 1 )
		return;
	// set the color
	float		color[4];
	const float	*regs = surf->shaderRegisters;
	color[0] = regs[pStage->color.registers[0]];
	color[1] = regs[pStage->color.registers[1]];
	color[2] = regs[pStage->color.registers[2]];
	color[3] = regs[pStage->color.registers[3]];

	// skip the entire stage if an add would be black
	if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) && color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
		return;
	}

	// skip the entire stage if a blend would be completely transparent
	if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) && color[3] <= 0 ) {
		return;
	}
	const float zero[4] = { 0, 0, 0, 0 };
	static const float one[4] = { 1, 1, 1, 1 };
	const float negOne[4] = { -color[0], -color[1], -color[2], -1 };

	GLColorOverride colorOverride;

	switch ( pStage->texture.texgen ) {
	case TG_SKYBOX_CUBE:
	case TG_WOBBLESKY_CUBE:
		//qglEnableVertexAttribArray( 8 );
		//qglVertexAttribPointer( 8, 3, GL_FLOAT, false, 0, vertexCache.VertexPosition( surf->dynamicTexCoords ) );
		programManager->cubeMapShader->Activate();
		{
			auto uniforms = programManager->cubeMapShader->GetUniformGroup<CubemapUniforms>();
			uniforms->skybox.Set( 1 );
			uniforms->modelMatrix.Set( surf->space->modelMatrix );
			idVec3 localViewOrigin;
			R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin );
			uniforms->viewOrigin.Set( localViewOrigin );
		}
		break;
	case TG_REFLECT_CUBE:
		colorOverride.Enable( color );
		break;
	case TG_SCREEN:
		colorOverride.Enable( color );
	default:
		programManager->oldStageShader->Activate();
		OldStageUniforms *oldStageUniforms = programManager->oldStageShader->GetUniformGroup<OldStageUniforms>();
		switch ( pStage->vertexColor )	 {
		case SVC_IGNORE:
			oldStageUniforms->colorMul.Set( zero );
			oldStageUniforms->colorAdd.Set( color );
			break;
		case SVC_MODULATE:
			// select the vertex color source
			oldStageUniforms->colorMul.Set( color );
			oldStageUniforms->colorAdd.Set( zero );
			break;
		case SVC_INVERSE_MODULATE:
			// select the vertex color source
			oldStageUniforms->colorMul.Set( negOne );
			oldStageUniforms->colorAdd.Set( color );
			break;
		}
	}
	auto prog = GLSLProgram::GetCurrentProgram();
	if ( prog ) {
		Uniforms::Global* transformUniforms = prog->GetUniformGroup<Uniforms::Global>();
		transformUniforms->Set( surf->space );
	}
	RB_PrepareStageTexturing( pStage, surf );

	// bind the texture
	RB_BindVariableStageImage( &pStage->texture, regs );

	// set the state
	GL_State( pStage->drawStateBits );

	// draw it
	RB_DrawElementsWithCounters( surf );

	RB_FinishStageTexturing( pStage, surf );

	switch ( pStage->texture.texgen ) {
	case TG_REFLECT_CUBE:
		break;
	case TG_SKYBOX_CUBE:
	case TG_WOBBLESKY_CUBE:
		programManager->cubeMapShader->GetUniformGroup<CubemapUniforms>()->skybox.Set( 0 );
	case TG_SCREEN:
	default:
		GLSLProgram::Deactivate();
	}
}

/*
==================
RB_STD_T_RenderShaderPasses_New

Extracted from the giantic loop in RB_STD_T_RenderShaderPasses
==================
*/
void RB_STD_T_RenderShaderPasses_ARB( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	if ( r_skipNewAmbient & 1 ) {
		return;
	}

	GL_State( pStage->drawStateBits );

	newShaderStage_t *newStage = pStage->newStage;
	qglBindProgramARB( GL_VERTEX_PROGRAM_ARB, newStage->vertexProgram );
	qglEnable( GL_VERTEX_PROGRAM_ARB );

	// megaTextures bind a lot of images and set a lot of parameters
	/*if ( newStage->megaTexture ) {
		newStage->megaTexture->SetMappingForSurface( surf->frontendGeo ); // FIXME
		idVec3	localViewer;
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewer );
		newStage->megaTexture->BindForViewOrigin( localViewer );
	}*/

	const float	*regs = surf->shaderRegisters;
	for ( int i = 0; i < newStage->numVertexParms; i++ ) {
		float	parm[4];
		parm[0] = regs[newStage->vertexParms[i][0]];
		parm[1] = regs[newStage->vertexParms[i][1]];
		parm[2] = regs[newStage->vertexParms[i][2]];
		parm[3] = regs[newStage->vertexParms[i][3]];
		qglProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, i, parm );
	}

	for ( int i = 0; i < newStage->numFragmentProgramImages; i++ ) {
		if ( newStage->fragmentProgramImages[i] ) {
			GL_SelectTexture( i );
			newStage->fragmentProgramImages[i]->Bind();
		}
	}
	qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, newStage->fragmentProgram );
	qglEnable( GL_FRAGMENT_PROGRAM_ARB );

	// draw it
	RB_DrawElementsWithCounters( surf );

	GL_SelectTexture( 0 );

	qglDisable( GL_VERTEX_PROGRAM_ARB );
	qglDisable( GL_FRAGMENT_PROGRAM_ARB );
}

void RB_STD_T_RenderShaderPasses_GLSL( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	if ( r_skipNewAmbient & 1 ) 
		return;

	newShaderStage_t *newStage = pStage->newStage;
	GL_State( pStage->drawStateBits );
	newStage->glslProgram->Activate();

	newStage->glslProgram->GetUniformGroup<Uniforms::Global>()->Set( surf->space );
	newStage->glslProgram->GetUniformGroup<Uniforms::MaterialStage>()->Set( pStage, surf );
	{
		using namespace Attributes::Default;
		//Attributes::Default::SetDrawVert( (size_t)ac, (1<<Position) | (1<<TexCoord) | (1<<Normal) | (1<<Tangent) | (1<<Bitangent));
	}
	RB_DrawElementsWithCounters( surf );

	GL_SelectTexture( 0 );
	GLSLProgram::Deactivate();
}

/*
==================
RB_STD_T_RenderShaderPasses_SoftParticle

Extracted from the giantic loop in RB_STD_T_RenderShaderPasses
==================
*/
void RB_STD_T_RenderShaderPasses_SoftParticle( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	// determine the blend mode (used by soft particles #3878)
	const int src_blend = pStage->drawStateBits & GLS_SRCBLEND_BITS;
	if ( (r_skipNewAmbient & 2) || !( src_blend == GLS_SRCBLEND_ONE || src_blend == GLS_SRCBLEND_SRC_ALPHA ) ) {
		return;
	}

	GLColorOverride colorOverride;

	// SteveL #3878. Particles are automatically softened by the engine, unless they have shader programs of
	// their own (i.e. are "newstages" handled above). This section comes after the newstage part so that if a
	// designer has specified their own shader programs, those will be used instead of the soft particle program.
	if ( pStage->vertexColor == SVC_IGNORE ) {
		// Ignoring vertexColor is not recommended for particles. The particle system uses vertexColor for fading.
		// However, there are existing particle effects that don't use it, in which case we default to using the
		// rgb color modulation specified in the material like the "old stages" do below.
		const float	*regs = surf->shaderRegisters;
		float		color[4];
		color[0] = regs[pStage->color.registers[0]];
		color[1] = regs[pStage->color.registers[1]];
		color[2] = regs[pStage->color.registers[2]];
		color[3] = regs[pStage->color.registers[3]];
		colorOverride.Enable( color );
	}

	// Disable depth clipping. The fragment program will handle it to allow overdraw.
	GL_State( pStage->drawStateBits | GLS_DEPTHFUNC_ALWAYS );

	if ( r_useGLSL ) {
		programManager->softParticleShader->Activate();
		programManager->softParticleShader->GetUniformGroup<Uniforms::Global>()->Set( surf->space );
	} else
		R_UseProgramARB( VPROG_SOFT_PARTICLE );

	// Bind image and _currentDepth
	GL_SelectTexture( 0 );
	pStage->texture.image->Bind();
	GL_SelectTexture( 1 );

	globalImages->currentDepthImage->Bind();

	// Set up parameters for fragment program
	// program.env[5] is the particle radius, given as { radius, 1/(faderange), 1/radius }
	float fadeRange;

	// fadeRange is the particle diameter for alpha blends (like smoke), but the particle radius for additive
	// blends (light glares), because additive effects work differently. Fog is half as apparent when a wall
	// is in the middle of it. Light glares lose no visibility when they have something to reflect off. See
	// issue #3878 for diagram
	if ( src_blend == GLS_SRCBLEND_SRC_ALPHA ) { // an alpha blend material
		fadeRange = surf->particle_radius * 2.0f;
	} else if ( src_blend == GLS_SRCBLEND_ONE ) { // an additive (blend add) material
		fadeRange = surf->particle_radius;
	}

	float params[4] = {
		surf->particle_radius,
		1.0f / ( fadeRange ),
		1.0f / surf->particle_radius,
		0.0f
	};

	// program.env[6] is the color channel mask. It gets added to the fade multiplier, so adding 1
	//    to a channel will make sure it doesn't get faded at all. Particles with additive blend
	//    need their RGB channels modifying to blend them out. Particles with an alpha blend need
	//    their alpha channel modifying.
	float blend[4];
	if ( src_blend == GLS_SRCBLEND_SRC_ALPHA ) { // an alpha blend material
		blend[0] = blend[1] = blend[2] = 1.0f; // Leave the rgb channels at full strength when fading
		blend[3] = 0.0f;						// but fade the alpha channel
	} else if ( src_blend == GLS_SRCBLEND_ONE ) { // an additive (blend add) material
		blend[0] = blend[1] = blend[2] = 0.0f; // Fade the rgb channels but
		blend[3] = 1.0f;						// leave the alpha channel at full strength
	}
	if ( r_useGLSL ) {
		auto group = programManager->softParticleShader->GetUniformGroup<Uniforms::SoftParticle>();
		group->softParticleParams.Set(params);
		group->softParticleBlend.Set(blend);
		//note: we need only u_scaleDepthCoords, but this is the easiest way to reuse code
		programManager->softParticleShader->GetUniformGroup<Uniforms::MaterialStage>()->Set(pStage, surf);
	}
	else {
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 5, params );
		qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 6, blend );
	}

	// draw it
	RB_DrawElementsWithCounters( surf );

	GL_SelectTexture( 0 );

	if ( !r_useGLSL ) {
		R_UseProgramARB();
	}
}

/*
==================
RB_STD_T_RenderShaderPasses_Frob

Frob shader stub
==================
*/
ID_NOINLINE void RB_STD_T_RenderShaderPasses_Frob( const shaderStage_t *pStage, const drawSurf_t *surf ) {
	if ( !r_newFrob )
		return;
	if ( surf->sort >= SS_DECAL ) // otherwise fills black
		return;

	programManager->frobShader->Activate();

	programManager->frobShader->GetUniformGroup<Uniforms::Global>()->Set( surf->space );
	auto frobUniforms = programManager->frobShader->GetUniformGroup<FrobUniforms>();
	frobUniforms->pulse.Set( .7 + .3 * sin( gameLocal.time * 1e-3 ) ); // FIXME move to frontend?
	if ( !surf->space )
		return;

	{
		using namespace Attributes::Default;
		//Attributes::Default::SetDrawVert( (size_t)ac, (1 << Position) | (1 << Normal) );
	}
	RB_DrawElementsWithCounters( surf );

	GL_SelectTexture( 0 );
	GLSLProgram::Deactivate();
}

/*
==================
RB_STD_T_RenderShaderPasses

This is also called for the generated 2D rendering
==================
*/
void RB_STD_T_RenderShaderPasses( const drawSurf_t *surf ) {
	int						stage;
	const idMaterial		*shader;
	const shaderStage_t		*pStage;
	const float				*regs;

	shader = surf->material;

	if ( !shader->HasAmbient() ) {
		return;
	}

	if ( shader->IsPortalSky() ) { // NB TDM portal sky does not use this flag or whatever mechanism
		return;    // it used to support. Our portalSky is drawn in this procedure using
	}

	// the skybox image captured in _currentRender. -- SteveL working on #4182
	if ( surf->material->GetSort() == SS_PORTAL_SKY && g_enablePortalSky.GetInteger() == 2 ) {
		return;
	}
	RB_LogComment( ">> RB_STD_T_RenderShaderPasses %s\n", surf->material->GetName() );

	// change the matrix if needed
	if ( !r_uniformTransforms.GetBool() && surf->space != backEnd.currentSpace ) {
		qglLoadMatrixf( surf->space->modelViewMatrix );
		backEnd.currentSpace = surf->space;
		RB_SetProgramEnvironmentSpace();
	}
	GL_CheckErrors();

	// change the scissor if needed
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		GL_ScissorVidSize( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
		            backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
		            backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
		            backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}
	GL_CheckErrors();

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !surf->numIndexes ) {
		return;
	}

	if ( !surf->ambientCache.IsValid() ) {
		common->Printf( "RB_T_RenderShaderPasses: !tri->ambientCache\n" );
		return;
	}

	// check whether we're drawing a soft particle surface #3878
	const bool soft_particle = ( surf->dsFlags & DSF_SOFT_PARTICLE ) != 0;

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	// set face culling appropriately
	GL_Cull( shader->GetCullType() );
	GL_CheckErrors();

	// set polygon offset if necessary
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
		GL_CheckErrors();
	}

	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
		GL_CheckErrors();
	}

	if ( surf->space->modelDepthHack != 0.0f && !soft_particle ) { // #3878 soft particles don't want modelDepthHack, which is
		// an older way to slightly "soften" particles
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
		GL_CheckErrors();
	}
	vertexCache.VertexPosition( surf->ambientCache );

	for ( stage = 0; stage < shader->GetNumStages() ; stage++ ) {
		pStage = shader->GetStage( stage );

		// check the enable condition
		if ( regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		// skip the stages involved in lighting
		if ( pStage->lighting != SL_AMBIENT ) {
			continue;
		}

		// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
		if ( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
			continue;
		}

		// see if we are a new-style stage
		newShaderStage_t *newStage = pStage->newStage;

		if ( newStage ) {
			//if ( newStage->GLSL || newStage->glslProgram )
			if ( r_useGLSL )
				RB_STD_T_RenderShaderPasses_GLSL( pStage, surf );
			else
				RB_STD_T_RenderShaderPasses_ARB( pStage, surf );
			continue;
		}

		if ( soft_particle && surf->particle_radius > 0.0f ) {
			RB_STD_T_RenderShaderPasses_SoftParticle( pStage, surf );
			continue;
		}
		RB_STD_T_RenderShaderPasses_OldStage( pStage, surf );
	}

	// reset polygon offset
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( surf->shaderRegisters[12] ) // even though FROB_SHADERPARM was defined as 11
		RB_STD_T_RenderShaderPasses_Frob( pStage, surf );

	if ( surf->space->weaponDepthHack || ( !soft_particle && surf->space->modelDepthHack != 0.0f ) ) {
		RB_LeaveDepthHack();
	}
	GL_CheckErrors();
}

/*
=====================
RB_STD_DrawShaderPasses

Draw non-light dependent passes
=====================
*/
int RB_STD_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	int	 i;

	// only obey skipAmbient if we are rendering a view
	if ( !numDrawSurfs ) {
		return numDrawSurfs;
	}
	GL_PROFILE( "STD_DrawShaderPasses" );

	RB_LogComment( "---------- RB_STD_DrawShaderPasses ----------\n" );

	// if we are about to draw the first surface that needs
	// the rendering in a texture, copy it over
	if ( drawSurfs[0]->sort >= SS_AFTER_FOG && !backEnd.viewDef->IsLightGem() ) {
		if ( r_skipPostProcess.GetBool() ) {
			return 0;
		}

		// only dump if in a 3d view
		if ( backEnd.viewDef->viewEntitys ) {
			frameBuffers->UpdateCurrentRenderCopy();
		}
		backEnd.currentRenderCopied = true;
	}
	GL_SelectTexture( 0 );

	RB_SetProgramEnvironment();
	GL_CheckErrors();

	// we don't use RB_RenderDrawSurfListWithFunction()
	// because we want to defer the matrix load because many
	// surfaces won't draw any ambient passes
	backEnd.currentSpace = NULL;

	for ( i = 0  ; i < numDrawSurfs ; i++ ) {
		if ( drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if ( drawSurfs[i]->sort >= SS_POST_PROCESS && !backEnd.currentRenderCopied ) {
			break;
		}

		if ( drawSurfs[i]->sort == SS_AFTER_FOG && !backEnd.afterFogRendered ) {
			break;
		}
		RB_STD_T_RenderShaderPasses( drawSurfs[i] );
		GL_CheckErrors();
	}
	GL_Cull( CT_FRONT_SIDED );

	return i;
}

/*
=============================================================================================

BLEND LIGHT PROJECTION

=============================================================================================
*/

/*
=====================
RB_T_BlendLight

=====================
*/
static void RB_T_BlendLight( const drawSurf_t *surf ) {
	if ( backEnd.currentSpace != surf->space ) {
		idPlane	lightProject[4];
		int		i;

		for ( i = 0 ; i < 4 ; i++ ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
		}
		BlendUniforms *blendUniforms = programManager->blendShader->GetUniformGroup<BlendUniforms>();
		blendUniforms->tex0PlaneS.Set( lightProject[0] );
		blendUniforms->tex0PlaneT.Set( lightProject[1] );
		blendUniforms->tex0PlaneQ.Set( lightProject[2] );
		blendUniforms->tex1PlaneS.Set( lightProject[3] );
	}

	// this gets used for both blend lights and shadow draws
	if ( surf->ambientCache.IsValid() ) {
		vertexCache.VertexPosition( surf->ambientCache );
	} else if ( surf->shadowCache.IsValid() ) {
		vertexCache.VertexPosition( surf->shadowCache, ATTRIB_SHADOW );
	}
	RB_DrawElementsWithCounters( surf );
}

/*
=====================
RB_BlendLight

Dual texture together the falloff and projection texture with a blend
mode to the framebuffer, instead of interacting with the surface texture
=====================
*/
static void RB_BlendLight( const drawSurf_t *drawSurfs,  const drawSurf_t *drawSurfs2 ) {
	const idMaterial	*lightShader;
	const shaderStage_t	*stage;
	int					i;
	const float	*regs;

	if ( !drawSurfs ) {
		return;
	}
	if ( r_skipBlendLights.GetBool() ) {
		return;
	}
	RB_LogComment( "---------- RB_BlendLight ----------\n" );

	lightShader = backEnd.vLight->lightShader;
	regs = backEnd.vLight->shaderRegisters;

	// texture 1 will get the falloff texture
	GL_SelectTexture( 1 );
	backEnd.vLight->falloffImage->Bind();

	// texture 0 will get the projected texture
	programManager->blendShader->Activate();
	BlendUniforms *blendUniforms = programManager->blendShader->GetUniformGroup<BlendUniforms>();

	for ( i = 0 ; i < lightShader->GetNumStages() ; i++ ) {
		stage = lightShader->GetStage( i );

		if ( !regs[ stage->conditionRegister ] ) {
			continue;
		}
		GL_State( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL );

		GL_SelectTexture( 0 );
		stage->texture.image->Bind();

		RB_LoadShaderTextureMatrix( regs, stage );

		// get the modulate values from the light, including alpha, unlike normal lights
		backEnd.lightColor[0] = regs[ stage->color.registers[0] ];
		backEnd.lightColor[1] = regs[ stage->color.registers[1] ];
		backEnd.lightColor[2] = regs[ stage->color.registers[2] ];
		backEnd.lightColor[3] = regs[ stage->color.registers[3] ];
		blendUniforms->blendColor.Set( backEnd.lightColor );

		RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_BlendLight );
		RB_RenderDrawSurfChainWithFunction( drawSurfs2, RB_T_BlendLight );

		RB_LoadShaderTextureMatrix( NULL, stage );
		/*if ( stage->texture.hasMatrix ) {
			GL_SelectTexture( 0 );
			qglMatrixMode( GL_TEXTURE );
			qglLoadIdentity();
			qglMatrixMode( GL_MODELVIEW );
		}*/
	}
	GL_SelectTexture( 0 );
	GLSLProgram::Deactivate();
}

//========================================================================

static idPlane	fogPlanes[2];

/*
=====================
RB_T_BasicFog
=====================
*/
static void RB_T_BasicFog( const drawSurf_t *surf ) {
	FogUniforms* fogUniforms = programManager->fogShader->GetUniformGroup<FogUniforms>();
	if ( backEnd.currentSpace != surf->space ) {

		idPlane	local;

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[0], local );
		local[3] += 0.5;
		fogUniforms->tex0PlaneS.Set( local );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogPlanes[1], local );
		local[3] += FOG_ENTER;
		fogUniforms->tex1PlaneT.Set( local );
	}
	if ( surf->material && surf->material->Coverage() == MC_TRANSLUCENT && surf->material->ReceivesLighting() )
		fogUniforms->fogAlpha.Set( surf->material->FogAlpha() );

	RB_T_RenderTriangleSurface( surf );
}

/*
==================
RB_FogPass
==================
*/
static void RB_FogPass( bool translucent ) {
	const srfTriangles_t *frustumTris;
	drawSurf_t			ds;
	const idMaterial	*lightShader;
	const shaderStage_t	*stage;
	const float			*regs;

	RB_LogComment( "---------- RB_FogPass ----------\n" );

	// create a surface for the light frustom triangles, which are oriented drawn side out
	frustumTris = backEnd.vLight->frustumTris;

	// if we ran out of vertex cache memory, skip it
	if ( !frustumTris->ambientCache.IsValid() ) {
		return;
	}
	memset( &ds, 0, sizeof( ds ) );

	if ( !backEnd.vLight->noFogBoundary ) { // No need to create the drawsurf if we're not fogging the bounding box -- #3664
		ds.space = &backEnd.viewDef->worldSpace;
		//ds.backendGeo = frustumTris;
		ds.frontendGeo = frustumTris; // FIXME JIC
		ds.numIndexes = frustumTris->numIndexes;
		ds.indexCache = frustumTris->indexCache;
		ds.ambientCache = frustumTris->ambientCache;
		ds.scissorRect = backEnd.viewDef->scissor;
	}

	// find the current color and density of the fog
	lightShader = backEnd.vLight->lightShader;
	regs = backEnd.vLight->shaderRegisters;

	// assume fog shaders have only a single stage
	stage = lightShader->GetStage( 0 );

	backEnd.lightColor[0] = regs[ stage->color.registers[0] ];
	backEnd.lightColor[1] = regs[ stage->color.registers[1] ];
	backEnd.lightColor[2] = regs[ stage->color.registers[2] ];
	backEnd.lightColor[3] = regs[ stage->color.registers[3] ];

	// calculate the falloff planes
	float	a;

	// if they left the default value on, set a fog distance of 500
	if ( backEnd.lightColor[3] <= 1.0 ) {
		a = -0.5f / DEFAULT_FOG_DISTANCE;
	} else {
		// otherwise, distance = alpha color
		a = -0.5f / backEnd.lightColor[3];
	}
	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );

	// texture 0 is the falloff image
	GL_SelectTexture( 0 );
	globalImages->fogImage->Bind();

	programManager->fogShader->Activate();
	FogUniforms *fogUniforms = programManager->fogShader->GetUniformGroup<FogUniforms>();
	fogUniforms->fogColor.Set( backEnd.lightColor );

	fogPlanes[0][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
	fogPlanes[0][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
	fogPlanes[0][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
	fogPlanes[0][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

	// texture 1 is the entering plane fade correction
	GL_SelectTexture( 1 );
	globalImages->fogEnterImage->Bind();

	// T will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
	fogPlanes[1][0] = 0.001f * backEnd.vLight->fogPlane[0];
	fogPlanes[1][1] = 0.001f * backEnd.vLight->fogPlane[1];
	fogPlanes[1][2] = 0.001f * backEnd.vLight->fogPlane[2];
	fogPlanes[1][3] = 0.001f * backEnd.vLight->fogPlane[3];

	// S is based on the view origin
	float s = backEnd.viewDef->renderView.vieworg * fogPlanes[1].Normal() + fogPlanes[1][3];
	fogUniforms->fogEnter.Set( FOG_ENTER + s );
	
	// 2.08: new fog
	static idCVarBool r_newFog( "r_newFog", "0", CVAR_RENDERER || CVAR_ARCHIVE, "alternative fog implementation" );
	if ( r_newFog.GetBool() ) {
		float distToFrustum = 0;
		for ( int i = 0; i < 6; i++ ) {
			auto& plane = backEnd.vLight->lightDef->frustum[i];
			float dist2Plane = plane.Distance( backEnd.viewDef->renderView.vieworg );
			if ( dist2Plane > distToFrustum )
				distToFrustum = dist2Plane;
		}
		fogUniforms->fogEnter.Set( distToFrustum );
		fogUniforms->fogDensity.Set( -a );
		fogUniforms->newFog.Set( 1 );
		fogUniforms->viewOrigin.Set( backEnd.viewDef->renderView.vieworg );
		fogUniforms->frustumPlanes.SetArray( 6, backEnd.vLight->lightDef->frustum[0].ToFloatPtr() );
	} else
		fogUniforms->newFog.Set( 0 );
	fogUniforms->fogAlpha.Set( 1 );

	if ( translucent ) {
		GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS );
		GL_Cull( CT_TWO_SIDED );
		RB_RenderDrawSurfChainWithFunction( backEnd.vLight->translucentInteractions, RB_T_BasicFog );
	} else {
		// draw it
		RB_RenderDrawSurfChainWithFunction( backEnd.vLight->globalInteractions, RB_T_BasicFog );
		RB_RenderDrawSurfChainWithFunction( backEnd.vLight->localInteractions, RB_T_BasicFog );

		if ( !backEnd.vLight->noFogBoundary ) { // Let mappers suppress fogging the bounding box -- SteveL #3664
			// the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
			// of depthfunc_equal
			GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS );
			GL_Cull( CT_BACK_SIDED );
			RB_RenderDrawSurfChainWithFunction( &ds, RB_T_BasicFog );
		}
	}

	GL_Cull( CT_FRONT_SIDED );

	GL_SelectTexture( 0 );
	GLSLProgram::Deactivate();
}

/*
==================
RB_STD_FogAllLights
==================
*/
void RB_STD_FogAllLights( bool translucent ) {
	if ( r_skipFogLights & 1 && !translucent || r_skipFogLights & 2 && translucent ||
		r_showOverDraw.GetInteger() != 0 ) {
		return;
	}
	GL_PROFILE( "STD_FogAllLights" );

	RB_LogComment( "---------- RB_STD_FogAllLights ----------\n" );

	for ( backEnd.vLight = backEnd.viewDef->viewLights ; backEnd.vLight; backEnd.vLight = backEnd.vLight->next ) {
		if ( !backEnd.vLight->lightShader->IsFogLight() && !backEnd.vLight->lightShader->IsBlendLight() ) {
			continue;
		}
		qglDisable( GL_STENCIL_TEST );

		if ( backEnd.vLight->lightShader->IsFogLight() ) {
			RB_FogPass( translucent );
		} 
		if ( translucent )
			continue;
		if ( backEnd.vLight->lightShader->IsBlendLight() ) {
			RB_BlendLight( backEnd.vLight->globalInteractions, backEnd.vLight->localInteractions );
		}
	}
	qglEnable( GL_STENCIL_TEST );
}

/*
=============
RB_STD_DrawView
=============
*/
void RB_STD_DrawView( void ) {
	GL_PROFILE( "STD_DrawView" );

	drawSurf_t	 **drawSurfs;
	int			numDrawSurfs, processed;

	RB_LogComment( "---------- RB_STD_DrawView ----------\n" );

	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

	drawSurfs = ( drawSurf_t ** )&backEnd.viewDef->drawSurfs[0];
	numDrawSurfs = backEnd.viewDef->numDrawSurfs;

	// clear the z buffer, set the projection matrix, etc
	RB_BeginDrawingView();
	GL_CheckErrors();

	backEnd.lightScale = r_lightScale.GetFloat();
	backEnd.overBright = 1.0f;

	// if we are just doing 2D rendering, no need to fill the depth buffer
	if ( backEnd.viewDef->viewEntitys ) {
		// fill the depth buffer and clear color buffer to black except on subviews
		RB_STD_FillDepthBuffer( drawSurfs, numDrawSurfs );
		GL_CheckErrors();
		if( ambientOcclusion->ShouldEnableForCurrentView() ) {
			ambientOcclusion->ComputeSSAOFromDepth();
		}

		RB_GLSL_DrawInteractions();
		GL_CheckErrors();
	}
		
	// now draw any non-light dependent shading passes
	processed = RB_STD_DrawShaderPasses( drawSurfs, numDrawSurfs );
	GL_CheckErrors();

	// fog and blend lights
	RB_STD_FogAllLights( false );

	// refresh fog and blend status 
	backEnd.afterFogRendered = true;

	// now draw any post-processing effects using _currentRender
	if ( processed < numDrawSurfs ) {
		RB_STD_DrawShaderPasses( drawSurfs + processed, numDrawSurfs - processed );
	}

	RB_STD_FogAllLights( true ); // 2.08: second fog pass, translucent only

	RB_RenderDebugTools( drawSurfs, numDrawSurfs );
	GL_CheckErrors();
}
