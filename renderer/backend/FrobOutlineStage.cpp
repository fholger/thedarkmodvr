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
#include "FrobOutlineStage.h"

#include "../glsl.h"
#include "../GLSLProgram.h"
#include "../GLSLProgramManager.h"
#include "../Profiling.h"
#include "../tr_local.h"
#include "../FrameBuffer.h"
#include "../FrameBufferManager.h"

idCVar r_frobIgnoreDepth( "r_frobIgnoreDepth", "1", CVAR_BOOL|CVAR_RENDERER|CVAR_ARCHIVE, "Ignore depth when drawing frob outline" );
idCVar r_frobDepthOffset( "r_frobDepthOffset", "0.004", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Extra depth offset for frob outline" );
idCVarBool r_frobOutline( "r_frobOutline", "1", CVAR_RENDERER | CVAR_ARCHIVE, "1 = draw outline around highlighted objects" );
idCVar r_frobOutlineColorR( "r_frobOutlineColorR", "1.0", CVAR_RENDERER | CVAR_FLOAT | CVAR_ARCHIVE , "Color of the frob outline - red component" );
idCVar r_frobOutlineColorG( "r_frobOutlineColorG", "1.0", CVAR_RENDERER | CVAR_FLOAT | CVAR_ARCHIVE , "Color of the frob outline - green component" );
idCVar r_frobOutlineColorB( "r_frobOutlineColorB", "1.0", CVAR_RENDERER | CVAR_FLOAT | CVAR_ARCHIVE , "Color of the frob outline - blue component" );
idCVar r_frobOutlineColorA( "r_frobOutlineColorA", "1.2", CVAR_RENDERER | CVAR_FLOAT | CVAR_ARCHIVE , "Color of the frob outline - alpha component" );
idCVar r_frobOutlineBlurPasses( "r_frobOutlineBlurPasses", "2", CVAR_RENDERER|CVAR_FLOAT|CVAR_ARCHIVE, "Thickness of the new frob outline" );

namespace {
	struct FrobOutlineUniforms : GLSLUniformGroup {
		UNIFORM_GROUP_DEF( FrobOutlineUniforms )

		DEFINE_UNIFORM( float, extrusion )
		DEFINE_UNIFORM( float, depth )
		DEFINE_UNIFORM( vec4, color )
	};

	struct BlurUniforms : GLSLUniformGroup {
		UNIFORM_GROUP_DEF( BlurUniforms )

		DEFINE_UNIFORM( sampler, source )
		DEFINE_UNIFORM( vec2, axis )
	};

	struct ApplyUniforms : GLSLUniformGroup {
		UNIFORM_GROUP_DEF( ApplyUniforms )

		DEFINE_UNIFORM( sampler, source )
		DEFINE_UNIFORM( vec4, color )
	};
}

void FrobOutlineStage::Init() {
	silhouetteShader = programManager->LoadFromFiles( "frob_silhouette", "stages/frob/frob.vert.glsl", "stages/frob/frob_silhouette.frag.glsl" );
	extrudeShader = programManager->LoadFromFiles( "frob_extrude", "stages/frob/frob.vert.glsl", "stages/frob/frob_silhouette.frag.glsl", "stages/frob/frob_extrude.geom.glsl" );
	applyShader = programManager->LoadFromFiles( "frob_apply", "fullscreen_tri.vert.glsl", "stages/frob/frob_apply.frag.glsl" );
	colorTex[0] = globalImages->ImageFromFunction( "frob_color_0", FB_RenderTexture );
	colorTex[1] = globalImages->ImageFromFunction( "frob_color_1", FB_RenderTexture );
	depthTex = globalImages->ImageFromFunction( "frob_depth", FB_RenderTexture );
	fbo[0] = frameBuffers->CreateFromGenerator( "frob_0", [this](FrameBuffer *) { this->CreateFbo( 0 ); } );
	fbo[1] = frameBuffers->CreateFromGenerator( "frob_1", [this](FrameBuffer *) { this->CreateFbo( 1 ); } );
	drawFbo = frameBuffers->CreateFromGenerator( "frob_draw", [this](FrameBuffer *) { this->CreateDrawFbo(); } );
}

void FrobOutlineStage::Shutdown() {}

void FrobOutlineStage::DrawFrobOutline( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	// find any surfaces that should be outlined
	idList<drawSurf_t *> outlineSurfs;
	for ( int i = 0; i < numDrawSurfs; ++i ) {
		drawSurf_t *surf = drawSurfs[i];

		if ( !surf->shaderRegisters[EXP_REG_PARM11] )
			continue;
		if ( !surf->material->HasAmbient() || !surf->numIndexes || !surf->ambientCache.IsValid() || !surf->space || surf->material->GetSort() >= SS_PORTAL_SKY )
			continue;

		outlineSurfs.AddGrow( surf );
	}

	if ( outlineSurfs.Num() == 0 )
		return;

	GL_PROFILE( "DrawFrobOutline" )

	GL_ScissorRelative( 0, 0, 1, 1 );

	MaskObjects( outlineSurfs );
	if ( !r_frobIgnoreDepth.GetBool() ) {
		MaskOutlines( outlineSurfs );
	}
	DrawSoftOutline( outlineSurfs );

	GL_State( GLS_DEPTHFUNC_EQUAL );
	qglStencilFunc( GL_ALWAYS, 255, 255 );
}

void FrobOutlineStage::CreateFbo( int idx ) {
	int width = frameBuffers->renderWidth / 4;
	int height = frameBuffers->renderHeight / 4;
	colorTex[idx]->GenerateAttachment( width, height, GL_R8 );
	fbo[idx]->Init( width, height );
	fbo[idx]->AddColorRenderTexture( 0, colorTex[idx] );
}

void FrobOutlineStage::CreateDrawFbo() {
	int width = frameBuffers->renderWidth / 4;
	int height = frameBuffers->renderHeight / 4;
	drawFbo->Init( width, height, 4 );
	drawFbo->AddColorRenderBuffer( 0, GL_R8 );
}

void FrobOutlineStage::MaskObjects( idList<drawSurf_t *> &surfs ) {
	// mark surfaces in the stencil buffer
	qglClearStencil( 0 );
	qglClear( GL_STENCIL_BUFFER_BIT );
	qglStencilFunc( GL_ALWAYS, 255, 255 );
	qglStencilOp( GL_KEEP, GL_REPLACE, GL_REPLACE );
	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHMASK | GLS_COLORMASK );
	silhouetteShader->Activate();
	FrobOutlineUniforms *frobUniforms = silhouetteShader->GetUniformGroup<FrobOutlineUniforms>();
	frobUniforms->extrusion.Set( 0.f );
	frobUniforms->depth.Set( 0.f );

	DrawObjects( surfs, silhouetteShader );
}

void FrobOutlineStage::MaskOutlines( idList<drawSurf_t *> &surfs ) {
	extrudeShader->Activate();
	// mask triangle outlines where depth test fails
	qglStencilFunc( GL_NOTEQUAL, 255, 255 );
	qglStencilOp( GL_KEEP, GL_REPLACE, GL_KEEP );
	GL_State( GLS_DEPTHFUNC_LESS | GLS_DEPTHMASK | GLS_COLORMASK );
	auto *uniforms = extrudeShader->GetUniformGroup<FrobOutlineUniforms>();
	uniforms->extrusion.Set( r_frobOutlineBlurPasses.GetFloat() * 20 );
	uniforms->depth.Set( r_frobDepthOffset.GetFloat() );

	DrawObjects( surfs, extrudeShader );
}

void FrobOutlineStage::DrawSoftOutline( idList<drawSurf_t *> &surfs ) {
	silhouetteShader->Activate();
	auto *silhouetteUniforms = silhouetteShader->GetUniformGroup<FrobOutlineUniforms>();
	silhouetteUniforms->color.Set( 1, 1, 1, 1 );
	// draw to small anti-aliased color buffer
	FrameBuffer *previousFbo = frameBuffers->activeFbo;
	drawFbo->Bind();
	GL_ViewportRelative( 0, 0, 1, 1 );
	GL_ScissorRelative( 0, 0, 1, 1 );
	GL_State( GLS_DEPTHFUNC_ALWAYS );
	qglClearColor( 0, 0, 0, 0 );
	qglClear( GL_COLOR_BUFFER_BIT );

	DrawObjects( surfs, silhouetteShader );

	// resolve color buffer
	drawFbo->BlitTo( fbo[0], GL_COLOR_BUFFER_BIT );

	// apply blur
	for ( int i = 0; i < r_frobOutlineBlurPasses.GetFloat(); ++i )
		ApplyBlur();

	previousFbo->Bind();
	GL_ViewportRelative( 0, 0, 1, 1 );
	GL_ScissorRelative( 0, 0, 1, 1 );

	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	qglStencilFunc( GL_NOTEQUAL, 255, 255 );
	GL_Cull( CT_FRONT_SIDED );
	applyShader->Activate();
	ApplyUniforms *applyUniforms = applyShader->GetUniformGroup<ApplyUniforms>();
	applyUniforms->source.Set( 0 );
	GL_SelectTexture( 0 );
	colorTex[0]->Bind();
	applyUniforms->color.Set( r_frobOutlineColorR.GetFloat(), r_frobOutlineColorG.GetFloat(), r_frobOutlineColorB.GetFloat(), r_frobOutlineColorA.GetFloat() );
	RB_DrawFullScreenTri();
}

void FrobOutlineStage::DrawObjects( idList<drawSurf_t *> &surfs, GLSLProgram  *shader ) {
	for ( drawSurf_t *surf : surfs ) {
		GL_Cull( surf->material->GetCullType() );
		shader->GetUniformGroup<Uniforms::Global>()->Set( surf->space );
		vertexCache.VertexPosition( surf->ambientCache );
		RB_DrawElementsWithCounters( surf );
	}
}

void FrobOutlineStage::ApplyBlur() {
	programManager->gaussianBlurShader->Activate();
	BlurUniforms *uniforms = programManager->gaussianBlurShader->GetUniformGroup<BlurUniforms>();
	uniforms->source.Set( 0 );

	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
	GL_SelectTexture( 0 );
	colorTex[0]->Bind();
	uniforms->axis.Set( 1, 0 );
	fbo[1]->Bind();
	qglClear(GL_COLOR_BUFFER_BIT);
	RB_DrawFullScreenTri();

	uniforms->axis.Set( 0, 1 );
	fbo[0]->Bind();
	colorTex[1]->Bind();
	qglClear(GL_COLOR_BUFFER_BIT);
	RB_DrawFullScreenTri();
}
