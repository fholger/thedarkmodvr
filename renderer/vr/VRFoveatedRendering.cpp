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
#include "VRFoveatedRendering.h"


#include "OpenXRBackend.h"
#include "../FrameBufferManager.h"
#include "../GLSLProgramManager.h"
#include "../GLSLUniforms.h"
#include "../Image.h"
#include "../Profiling.h"

idCVar vr_useFixedFoveatedRendering("vr_useFixedFoveatedRendering", "0", CVAR_INTEGER|CVAR_RENDERER|CVAR_ARCHIVE, "Enable fixed foveated rendering.");
idCVar vr_foveatedInnerRadius("vr_foveatedInnerRadius", "0.3", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Inner foveated radius to render at full resolution");
idCVar vr_foveatedMidRadius("vr_foveatedMidRadius", "0.75", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Middle foveated radius to render at half resolution");
idCVar vr_foveatedOuterRadius("vr_foveatedOuterRadius", "0.85", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Outer foveated radius to render at quarter resolution - anything outside is rendered at 1/16th resolution");
idCVar vr_foveatedReconstructionQuality("vr_foveatedReconstructionQuality", "2", CVAR_INTEGER|CVAR_RENDERER|CVAR_ARCHIVE, "The quality of the reconstruction for foveated rendering");

namespace {
	void VR_CreateVariableRateShadingImage( idImage *image ) {
		image->type = TT_2D;
		image->uploadWidth = frameBuffers->renderWidth;
		image->uploadHeight = frameBuffers->renderHeight;
		qglGenTextures( 1, &image->texnum );
		qglBindTexture( GL_TEXTURE_2D, image->texnum );

		GLint texelW, texelH;
		qglGetIntegerv( GL_SHADING_RATE_IMAGE_TEXEL_WIDTH_NV, &texelW );
		qglGetIntegerv( GL_SHADING_RATE_IMAGE_TEXEL_HEIGHT_NV, &texelH );
		GLsizei imageWidth = image->uploadWidth / texelW;
		GLsizei imageHeight = image->uploadHeight / texelH;
		GLsizei centerX = imageWidth / 2;
		GLsizei centerY = imageHeight / 2;
		float outerRadius = vr_foveatedOuterRadius.GetFloat() * 0.5f * imageHeight;
		float midRadius = vr_foveatedMidRadius.GetFloat() * 0.5f * imageHeight;
		float innerRadius = vr_foveatedInnerRadius.GetFloat() * 0.5f * imageHeight;

		idList<byte> vrsData;
		vrsData.SetNum( imageWidth * imageHeight );
		for ( int y = 0; y < imageHeight; ++y ) {
			for ( int x = 0; x < imageWidth; ++x ) {
				float distFromCenter = idMath::Sqrt( (x-centerX)*(x-centerX) + (y-centerY)*(y-centerY));
				if ( distFromCenter >= outerRadius) {
					vrsData[y*imageWidth + x] = 3;
				} else if ( distFromCenter >= midRadius) {
					vrsData[y*imageWidth + x] = 2;
				} else if ( distFromCenter >= innerRadius) {
					vrsData[y*imageWidth + x] = 1;
				} else {
					vrsData[y*imageWidth + x] = 0;
				}
			}
		}

		qglTexStorage2D( GL_TEXTURE_2D, 1, GL_R8UI, imageWidth, imageHeight );
		qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, imageWidth, imageHeight, GL_RED_INTEGER, GL_UNSIGNED_BYTE, vrsData.Ptr() );
		
		GL_SetDebugLabel( GL_TEXTURE, image->texnum, image->imgName );
	}

	struct RadialDensityMaskUniforms : GLSLUniformGroup {
		UNIFORM_GROUP_DEF( RadialDensityMaskUniforms )
		
		DEFINE_UNIFORM( vec2, center )
		DEFINE_UNIFORM( vec3, radius )
		DEFINE_UNIFORM( vec2, invClusterResolution )
	};

	struct RdmReconstructUniforms : GLSLUniformGroup {
		UNIFORM_GROUP_DEF( RdmReconstructUniforms )

		DEFINE_UNIFORM( sampler, srcTex )
		DEFINE_UNIFORM( sampler, dstTex )
		DEFINE_UNIFORM( vec2, center )
		DEFINE_UNIFORM( vec2, invClusterResolution )
		DEFINE_UNIFORM( vec2, invResolution )
		DEFINE_UNIFORM( vec3, radius )
		DEFINE_UNIFORM( int, quality )
	};
}

void VRFoveatedRendering::Init() {
	if ( GLAD_GL_NV_shading_rate_image ) {
		variableRateShadingImage = globalImages->ImageFromFunction( "vrsImage", VR_CreateVariableRateShadingImage );
	}
	rdmReconstructionImage = globalImages->ImageFromFunction( "rdm_reconstruct", FB_RenderTexture );
	rdmReconstructionFbo = frameBuffers->CreateFromGenerator( "rdm_reconstruction_fbo", [this](FrameBuffer *fbo) {
		int width = frameBuffers->renderWidth;
		int height = frameBuffers->renderHeight;
		rdmReconstructionImage->GenerateAttachment( width, height, GL_RGBA8, GL_LINEAR );
		fbo->Init( width, height );
		fbo->AddColorRenderTexture( 0, rdmReconstructionImage );
	} );
	radialDensityMaskShader = programManager->LoadFromFiles( "radial_density_mask", "vr/depth_mask.vert.glsl", "vr/radial_density_mask.frag.glsl" );
	rdmReconstructShader = programManager->LoadComputeShader( "rdm_reconstruct", "vr/radial_density_mask_reconstruct.compute.glsl" );
}

void VRFoveatedRendering::Destroy() {
	radialDensityMaskShader = nullptr;
	rdmReconstructShader = nullptr;
	if ( variableRateShadingImage != nullptr ) {
		variableRateShadingImage->PurgeImage();
		variableRateShadingImage = nullptr;
	}
}

void VRFoveatedRendering::PrepareVariableRateShading() {
	if ( !GLAD_GL_NV_shading_rate_image ) {
		return;
	}

	if ( vr_useFixedFoveatedRendering.IsModified() || vr_foveatedInnerRadius.IsModified() || vr_foveatedMidRadius.IsModified() || vr_foveatedOuterRadius.IsModified() ) {
		// ensure VRS image is up to date
		variableRateShadingImage->PurgeImage();
		vr_useFixedFoveatedRendering.ClearModified();
		vr_foveatedInnerRadius.ClearModified();
		vr_foveatedMidRadius.ClearModified();
		vr_foveatedOuterRadius.ClearModified();
	}

	if ( vr_useFixedFoveatedRendering.GetInteger() != 1 ) {
		return;
	}

	if ( variableRateShadingImage->uploadWidth != frameBuffers->renderWidth
			|| variableRateShadingImage->uploadHeight != frameBuffers->renderHeight ) {
		variableRateShadingImage->PurgeImage();
	}

	// ensure image is constructed
	variableRateShadingImage->Bind();
	qglBindShadingRateImageNV( variableRateShadingImage->texnum );
	GLenum palette[3];
	palette[0] = GL_SHADING_RATE_1_INVOCATION_PER_PIXEL_NV;
	palette[1] = GL_SHADING_RATE_1_INVOCATION_PER_1X2_PIXELS_NV;
	palette[2] = GL_SHADING_RATE_1_INVOCATION_PER_2X2_PIXELS_NV;
	palette[3] = GL_SHADING_RATE_1_INVOCATION_PER_4X4_PIXELS_NV;
	qglShadingRateImagePaletteNV( 0, 0, sizeof(palette)/sizeof(GLenum), palette );
}

void VRFoveatedRendering::DrawRadialDensityMaskToDepth( int eye ) {
	radialDensityMaskShader->Activate();
	auto uniforms = radialDensityMaskShader->GetUniformGroup<RadialDensityMaskUniforms>();
	uniforms->center.Set( vrBackend->ProjectCenterUV( eye ) );
	uniforms->radius.Set( vr_foveatedInnerRadius.GetFloat(), vr_foveatedMidRadius.GetFloat(), vr_foveatedOuterRadius.GetFloat() );
	uniforms->invClusterResolution.Set( 8.f / frameBuffers->primaryFbo->Width(), 8.f / frameBuffers->primaryFbo->Height() );

	RB_DrawFullScreenQuad();
}

void VRFoveatedRendering::ReconstructImageFromRdm( int eye ) {
	GL_PROFILE("RdmReconstruction")

	rdmReconstructionFbo->Bind();

	int width = rdmReconstructionImage->uploadWidth;
	int height = rdmReconstructionImage->uploadHeight;
	rdmReconstructShader->Activate();
	auto uniforms = rdmReconstructShader->GetUniformGroup<RdmReconstructUniforms>();
	uniforms->srcTex.Set( 0 );
	uniforms->dstTex.Set( 0 );
	uniforms->center.Set( vrBackend->ProjectCenterUV( eye ) );
	uniforms->invClusterResolution.Set( 8.f/width, 8.f/height );
	uniforms->invResolution.Set( 1.f/width, 1.f/height );
	uniforms->radius.Set( vr_foveatedInnerRadius.GetFloat(), vr_foveatedMidRadius.GetFloat(), vr_foveatedOuterRadius.GetFloat() );
	uniforms->quality.Set( vr_foveatedReconstructionQuality.GetInteger() );

	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();
	qglBindImageTexture( 0, rdmReconstructionImage->texnum, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8 );
	
	qglDispatchCompute( (width + 7) / 8, (height + 7) / 8, 1 );
	qglMemoryBarrier( GL_FRAMEBUFFER_BARRIER_BIT );

	FrameBuffer *targetFbo = r_tonemap ? frameBuffers->guiFbo : frameBuffers->defaultFbo;
	rdmReconstructionFbo->BlitTo( targetFbo, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	targetFbo->Bind();
}

