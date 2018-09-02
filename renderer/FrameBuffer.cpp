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
#include "FrameBuffer.h"
#include "glsl.h"

// all false at start
bool primaryOn = false, shadowOn = false;
bool depthCopiedThisView = false;
GLuint fboPrimary, fboResolve, fboPostProcess, fboCurrent, pbo;
std::vector<GLuint> fboShadows;
GLuint renderBufferColor, renderBufferDepthStencil, renderBufferPostProcess;
GLuint postProcessWidth, postProcessHeight;
uint ShadowFboIndex;
float shadowResolution;

void FB_CreatePrimaryResolve( GLuint width, GLuint height, int msaa ) {
	if ( !fboPrimary ) {
		qglGenFramebuffers( 1, &fboPrimary );
	}

	if ( !renderBufferColor ) {
		qglGenRenderbuffers( 1, &renderBufferColor );
	}

	if ( msaa > 1 ) {
		if ( !fboResolve ) {
			qglGenFramebuffers( 1, &fboResolve );
		}

		if ( !renderBufferDepthStencil ) {
			qglGenRenderbuffers( 1, &renderBufferDepthStencil );
		}
		qglBindRenderbuffer( GL_RENDERBUFFER, renderBufferColor );
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, msaa, GL_RGBA, width, height );
		qglBindRenderbuffer( GL_RENDERBUFFER, renderBufferDepthStencil );
		// revert to old behaviour, switches are to specific
		qglRenderbufferStorageMultisample( GL_RENDERBUFFER, msaa, ( r_fboDepthBits.GetInteger() == 32 ) ? GL_DEPTH32F_STENCIL8 : GL_DEPTH24_STENCIL8, width, height );
		qglBindRenderbuffer( GL_RENDERBUFFER, 0 );
		qglBindFramebuffer( GL_FRAMEBUFFER, fboPrimary );
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderBufferColor );
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, renderBufferDepthStencil );
		common->Printf( "Generated render buffers for COLOR & DEPTH/STENCIL: %dx%dx%d\n", width, height, msaa );
	} else {
		// only need the color render buffer, depth will be bound directly to texture
		qglBindRenderbuffer( GL_RENDERBUFFER, renderBufferColor );
		qglRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA, width, height );
		qglBindFramebuffer( GL_FRAMEBUFFER, fboPrimary );
		qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderBufferColor );
	}
}

void FB_CreatePostProcess( GLuint width, GLuint height ) {
	if ( !fboPostProcess ) {
		qglGenFramebuffers( 1, &fboPostProcess );
	}
	if ( !renderBufferPostProcess ) {
		qglGenRenderbuffers( 1, &renderBufferPostProcess );
	}
	qglBindRenderbuffer( GL_RENDERBUFFER, renderBufferPostProcess );
	qglRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA, width, height );
	qglBindRenderbuffer( GL_RENDERBUFFER, 0 );
	qglBindFramebuffer( GL_FRAMEBUFFER, fboPostProcess );
	qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderBufferPostProcess );
	int status = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( GL_FRAMEBUFFER_COMPLETE != status ) {
		common->Printf( "glCheckFramebufferStatus postProcess: %d\n", status );
		qglDeleteFramebuffers( 1, &fboPostProcess );
		fboPostProcess = 0; // try from scratch next time
	}
	postProcessWidth = width;
	postProcessHeight = height;
}

/*
When using AA, we can't just qglCopyTexImage2D from MSFB to a regular texture.
This function blits to fboResolve, then we have a copy of MSFB in the currentRender texture
*/
void FB_ResolveMultisampling( GLbitfield mask, GLenum filter ) {
	qglDisable( GL_SCISSOR_TEST );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, fboResolve );
	qglBlitFramebuffer( 0, 0, globalImages->currentRenderImage->uploadWidth,
	                    globalImages->currentRenderImage->uploadHeight,
	                    0, 0, globalImages->currentRenderImage->uploadWidth,
	                    globalImages->currentRenderImage->uploadHeight,
	                    mask, filter );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, fboPrimary );
	qglEnable( GL_SCISSOR_TEST );
}

/*
This function blits to fboShadow as a resolver to have a workable copy of the stencil texture
*/
void FB_ResolveShadowAA() {
	qglDisable( GL_SCISSOR_TEST );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, fboShadows[0] );
	qglBlitFramebuffer( 0, 0, globalImages->currentRenderImage->uploadWidth,
	                    globalImages->currentRenderImage->uploadHeight,
	                    0, 0, globalImages->currentRenderImage->uploadWidth,
	                    globalImages->currentRenderImage->uploadHeight,
	                    GL_STENCIL_BUFFER_BIT, GL_NEAREST );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, fboPrimary );
	qglEnable( GL_SCISSOR_TEST );
}

/*
called when post-processing is about to start, needs pixels
we need to copy render separately for water/smoke and then again for bloom
*/
void FB_CopyColorBuffer() {
	if ( primaryOn && r_multiSamples.GetInteger() > 1 ) {
		FB_ResolveMultisampling( GL_COLOR_BUFFER_BIT );
	} else {
		GL_SelectTexture( 0 );
		globalImages->currentRenderImage->CopyFramebuffer(
		    backEnd.viewDef->viewport.x1,
		    backEnd.viewDef->viewport.y1,
		    backEnd.viewDef->viewport.x2 -
		    backEnd.viewDef->viewport.x1 + 1,
		    backEnd.viewDef->viewport.y2 -
		    backEnd.viewDef->viewport.y1 + 1, true );
	}
}

void FB_CopyDepthBuffer() {
	// AA off: already have depth texture attached to FBO. AA on: need to blit from multisampled storage
	if ( primaryOn && r_multiSamples.GetInteger() > 1 ) {
		FB_ResolveMultisampling( GL_DEPTH_BUFFER_BIT );
	}
}

// system mem only
void FB_CopyRender( const copyRenderCommand_t &cmd ) {
	//stgatilov #4754: this happens during lightgem calculating in minimized windowed TDM
	if ( cmd.imageWidth * cmd.imageHeight == 0 ) {
		return;	//no pixels to be read
	}
	int backEndStartTime = Sys_Milliseconds();

	if ( !primaryOn ) { // #4425: not applicable, raises gl errors
		qglReadBuffer( GL_BACK );
	}

	if ( primaryOn && r_multiSamples.GetInteger() > 1 ) {
		FB_ResolveMultisampling( GL_COLOR_BUFFER_BIT );
		qglBindFramebuffer( GL_FRAMEBUFFER, fboResolve );
	}

	// #4395 lightem pixel pack buffer optimization
	if ( cmd.buffer ) {
		if ( cmd.usePBO && glConfig.pixelBufferAvailable ) {
			static int pboSize = -1;

			if ( !pbo ) {
				pboSize = cmd.imageWidth * cmd.imageHeight * 3;
				qglGenBuffersARB( 1, &pbo );
				qglBindBufferARB( GL_PIXEL_PACK_BUFFER, pbo );
				qglBufferDataARB( GL_PIXEL_PACK_BUFFER, pboSize, NULL, GL_STREAM_READ );
				qglBindBufferARB( GL_PIXEL_PACK_BUFFER, 0 );
			}

			if ( cmd.imageWidth * cmd.imageHeight * 3 != pboSize ) {
				common->Error( "CaptureRenderToBuffer: wrong PBO size %dx%d/%d", cmd.imageWidth, cmd.imageHeight, pboSize );
			}
			qglBindBufferARB( GL_PIXEL_PACK_BUFFER, pbo );

			byte *ptr = reinterpret_cast< byte * >( qglMapBufferARB( GL_PIXEL_PACK_BUFFER, GL_READ_ONLY ) );

			// #4395 moved to initializer
			if ( ptr ) {
				memcpy( cmd.buffer, ptr, pboSize );
				qglUnmapBufferARB( GL_PIXEL_PACK_BUFFER );
			}

			// revelator: added c++11 nullptr
			qglReadPixels( cmd.x, cmd.y, cmd.imageWidth, cmd.imageHeight, GL_RGB, GL_UNSIGNED_BYTE, nullptr );
			qglBindBufferARB( GL_PIXEL_PACK_BUFFER, 0 );
		} else {
			qglReadPixels( cmd.x, cmd.y, cmd.imageWidth, cmd.imageHeight, GL_RGB, GL_UNSIGNED_BYTE, cmd.buffer );
		}
	}

	if ( cmd.image ) {
		cmd.image->CopyFramebuffer( cmd.x, cmd.y, cmd.imageWidth, cmd.imageHeight, false );
	}

	if ( primaryOn && r_multiSamples.GetInteger() > 1 ) {
		qglBindFramebuffer( GL_FRAMEBUFFER, fboPrimary );
	}
	int backEndFinishTime = Sys_Milliseconds();
	backEnd.pc.msec += backEndFinishTime - backEndStartTime;
	GL_CheckErrors();
}

// force recreate on a cvar change
void DeleteFramebuffers() {
	qglDeleteFramebuffers( 1, &fboPrimary );
	qglDeleteFramebuffers( 1, &fboResolve );
	qglDeleteFramebuffers( int(fboShadows.size()), &fboShadows[0] );
	fboPrimary = 0;
	fboResolve = 0;
	fboShadows.clear();
}

void CheckCreatePrimary() {
	// debug
	GL_CheckErrors();

	// virtual resolution as a modern alternative for actual desktop resolution affecting all other windows
	GLuint curWidth = r_fboResolution.GetFloat() * glConfig.vidWidth, curHeight = r_fboResolution.GetFloat() * glConfig.vidHeight;

	if ( r_fboSeparateStencil.IsModified() || ( curWidth != globalImages->currentRenderImage->uploadWidth ) ) {
		r_fboSeparateStencil.ClearModified();
		DeleteFramebuffers(); // otherwise framebuffer is not resized (even though its attachments are, FIXME? ViewPort not updated?)
	}

	if ( r_fboDepthBits.IsModified() && r_multiSamples.GetInteger() > 1 ) {
		r_fboDepthBits.ClearModified();
		DeleteFramebuffers(); // Intel wants us to keep depth texture and multisampled storage formats the same
	}

	// intel optimization
	if ( r_fboSeparateStencil.GetBool() ) {
		globalImages->currentDepthImage->GenerateAttachment( curWidth, curHeight, GL_DEPTH );
		if ( !r_softShadowsQuality.GetBool() ) // currentStencilFbo will be initialized in CheckCreateShadow with possibly different resolution
			globalImages->currentStencilFbo->GenerateAttachment( curWidth, curHeight, GL_STENCIL );
	} else { // AMD/nVidia fast enough already, separate depth/stencil not supported
		globalImages->currentDepthImage->GenerateAttachment( curWidth, curHeight, GL_DEPTH_STENCIL );
	}

	// this texture is now only used as screen copy for post processing, never as FBO attachment in any mode, but we still need to set its size and other params here
	globalImages->currentRenderImage->GenerateAttachment( curWidth, curHeight, GL_COLOR );

	// (re-)attach textures to FBO
	if ( !fboPrimary || r_multiSamples.IsModified() ) {
		r_multiSamples.ClearModified();
		int msaa = r_multiSamples.GetInteger();
		FB_CreatePrimaryResolve( curWidth, curHeight, msaa );
		qglBindFramebuffer( GL_FRAMEBUFFER, ( msaa > 1 ) ? fboResolve : fboPrimary );

		if ( msaa > 1 ) {
			// attach a texture to FBO color attachment point
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, globalImages->currentRenderImage->texnum, 0 );
		}

		// attach a texture to depth attachment point
		GLuint depthTex = globalImages->currentDepthImage->texnum;

		// intel optimization
		if ( r_fboSeparateStencil.GetBool() ) {
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0 );
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, globalImages->currentStencilFbo->texnum, 0 );
		} else {
			// shorthand for "both depth and stencil"
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0 );
		}
		int statusResolve = qglCheckFramebufferStatus( GL_FRAMEBUFFER );

		qglBindFramebuffer( GL_FRAMEBUFFER, fboPrimary );

		int statusPrimary = qglCheckFramebufferStatus( GL_FRAMEBUFFER );

		// something went wrong, fall back to default
		if ( GL_FRAMEBUFFER_COMPLETE != statusResolve || GL_FRAMEBUFFER_COMPLETE != statusPrimary ) {
			common->Printf( "glCheckFramebufferStatus - primary: %d - resolve: %d\n", statusPrimary, statusResolve );
			DeleteFramebuffers(); // try from scratch next time
			r_useFbo.SetBool( false );
			r_softShadowsQuality.SetInteger( 0 );
		}
		Framebuffer::BindPrimary();
	}
}

void CheckCreateShadow() {
	// (re-)attach textures to FBO
	GLuint curWidth = glConfig.vidWidth;
	GLuint curHeight = glConfig.vidHeight;
	if ( primaryOn ) {
		float shadowRes = 1.0f;
		if ( r_softShadowsQuality.GetInteger() < 0 ) {
			shadowRes = r_softShadowsQuality.GetFloat() / -100.0f;
		}

		curWidth *= r_fboResolution.GetFloat() * shadowRes;
		curHeight *= r_fboResolution.GetFloat() * shadowRes;
	}
	textureType_t type = r_shadows.GetInteger() == 2 ? TT_CUBIC : TT_2D;
	static textureType_t nowType;

	// reset textures
	if ( r_fboSeparateStencil.GetBool() ) {
		// currentDepthImage is initialized there
		CheckCreatePrimary();
		globalImages->currentStencilFbo->GenerateAttachment( curWidth, curHeight, GL_STENCIL );
	} else {
		globalImages->shadowDepthFbo->GenerateAttachment( curWidth, curHeight, GL_DEPTH_STENCIL );
	}
	auto *shadowCubeMap = globalImages->shadowCubeMap[ShadowFboIndex % MAX_SHADOW_MAPS];

	if ( shadowCubeMap->uploadWidth != r_shadowMapSize.GetInteger() || r_fboDepthBits.IsModified() ) {
		r_fboDepthBits.ClearModified();
		shadowCubeMap->Bind();
		shadowCubeMap->uploadWidth = r_shadowMapSize.GetInteger();
		shadowCubeMap->uploadHeight = r_shadowMapSize.GetInteger();

		for ( int sideId = 0; sideId < 6; sideId++ ) {
			// revert back again, the problem seems to be stencil depth.
			switch ( r_fboDepthBits.GetInteger() ) {
			case 16:
				qglTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + sideId, 0, GL_DEPTH_COMPONENT16, r_shadowMapSize.GetInteger(), r_shadowMapSize.GetInteger(), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr );
				break;
			case 32:
				qglTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + sideId, 0, GL_DEPTH_COMPONENT32F, r_shadowMapSize.GetInteger(), r_shadowMapSize.GetInteger(), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr );
				break;
			default:
				qglTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + sideId, 0, GL_DEPTH_COMPONENT24, r_shadowMapSize.GetInteger(), r_shadowMapSize.GetInteger(), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr );
				break;
			}
		}
		qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		//qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
		qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
		qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );

		//qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE );
		qglTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );

		// 3.2 required for geometry shader anyway but still don't want crashes
		if ( qglGenerateMipmap ) {
			qglGenerateMipmap( GL_TEXTURE_CUBE_MAP );
		}
		globalImages->BindNull();
	}
	if ( ShadowFboIndex >= fboShadows.size() || nowType != type ) {
		GLuint fboShadow;
		if ( ShadowFboIndex >= fboShadows.size() ) {
			qglGenFramebuffers( 1, &fboShadow );
			fboShadows.push_back( fboShadow );
		} else
			fboShadow = fboShadows[ShadowFboIndex];
		qglBindFramebuffer( GL_FRAMEBUFFER, fboShadow );

		if ( r_shadows.GetInteger() == 2 ) {
			GLuint depthTex = shadowCubeMap->texnum;
			qglFramebufferTexture( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTex, ShadowFboIndex / MAX_SHADOW_MAPS );
			qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );
		} else {
			if ( r_fboSeparateStencil.GetBool() ) {
				qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, globalImages->currentDepthImage->texnum, 0 );
				qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, globalImages->currentStencilFbo->texnum, 0 );
			} else {
				GLuint depthTex = globalImages->shadowDepthFbo->texnum;
				qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0 );
			}
		}
		int status = qglCheckFramebufferStatus( GL_FRAMEBUFFER );

		// something went wrong, fall back to default
		if ( GL_FRAMEBUFFER_COMPLETE != status ) {
			common->Printf( "glCheckFramebufferStatus shadow: %d\n", status );
			qglDeleteFramebuffers( 1, &fboShadow );
			fboShadow = 0; // try from scratch next time
		}
		Framebuffer::BindPrimary();
		nowType = type;
	}
	GL_CheckErrors();
}

void FB_SelectPrimary() {
	if ( primaryOn ) {
		qglBindFramebuffer( GL_FRAMEBUFFER, fboPrimary );
	}
}

void FB_SelectPostProcess() {
	if ( !primaryOn ) {
		return;
	}
	GLuint curWidth = glConfig.vidWidth * r_fboResolution.GetFloat();
	GLuint curHeight = glConfig.vidHeight * r_fboResolution.GetFloat();

	if ( !fboPostProcess || curWidth != postProcessWidth || curHeight != postProcessHeight ) {
		FB_CreatePostProcess( curWidth, curHeight );
	}
	qglBindFramebuffer( GL_FRAMEBUFFER, fboPostProcess );
}

/*
Soft shadows vendor specific implementation
Intel: separate stencil buffer, direct access, awesome
Others: combined stencil & depth, copy to a separate FBO, meh
*/
void FB_BindShadowTexture() {
	GL_CheckErrors();
	if ( r_shadows.GetInteger() == 2 ) {
		GL_SelectTexture( 6 );
		globalImages->shadowCubeMap[0]->Bind();
	} else {
		GL_SelectTexture( 6 );
		globalImages->currentDepthImage->Bind();
		GL_SelectTexture( 7 );

		if ( !r_fboSeparateStencil.GetBool() ) {
			globalImages->shadowDepthFbo->Bind();
			qglTexParameteri( GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_STENCIL_INDEX );
		} else {
			globalImages->currentStencilFbo->Bind();
		}
	}
	GL_CheckErrors();
}

// accidentally deleted
void FB_ApplyScissor() {
	// run once update on change
	bool runOnce = false;
	if ( r_useScissor.GetBool() && !runOnce ) {
		float resFactor = 1.0f;
		if ( shadowOn ) {
			resFactor *= shadowResolution;
		}
		GL_Scissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1 * resFactor,
		            backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1 * resFactor,
		            backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1 * resFactor,
		            backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 * resFactor );
		runOnce = true;
	}
}

void FB_ToggleShadow( bool on, bool clear ) {
	CheckCreateShadow();
	if ( on && r_shadows.GetInteger() == 1 ) {
		// most vendors can't do separate stencil so we need to copy depth from the main/default FBO
		if ( !depthCopiedThisView && !r_fboSeparateStencil.GetBool() ) {
			if( primaryOn && r_multiSamples.GetInteger() > 1 ) {
				FB_ResolveMultisampling( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
				qglBindFramebuffer( GL_READ_FRAMEBUFFER, fboResolve );
			}

			qglDisable( GL_SCISSOR_TEST );
			qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, fboShadows[0] );
			qglBlitFramebuffer( 0, 0, globalImages->currentRenderImage->uploadWidth, globalImages->currentRenderImage->uploadHeight,
				0, 0, globalImages->currentRenderImage->uploadWidth, globalImages->currentRenderImage->uploadHeight,
				GL_DEPTH_BUFFER_BIT, GL_NEAREST );
			qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, primaryOn ? fboPrimary : 0 );
			qglEnable( GL_SCISSOR_TEST );
			depthCopiedThisView = true;
		}
		GL_CheckErrors();
	}
	qglBindFramebuffer( GL_FRAMEBUFFER, on ? fboShadows[ShadowFboIndex] : primaryOn ? fboPrimary : 0 );
	if( on && r_shadows.GetInteger() == 1 && r_multiSamples.GetInteger() > 1 && r_softShadowsQuality.GetInteger() >= 0 ) {
		// with MSAA on, we need to render against the multisampled primary buffer, otherwise stencil is drawn
		// against a lower-quality depth map which may cause render errors with shadows
		qglBindFramebuffer( GL_FRAMEBUFFER, fboPrimary );
	}
	if (!on && !primaryOn) {
		Framebuffer::BindPrimary();
	}

	// stencil softshadows
	if ( r_shadows.GetInteger() == 1 ) {
		shadowOn = on;
		if ( r_softShadowsQuality.GetInteger() < 0 ) {
			if ( on ) {
				shadowResolution = r_softShadowsQuality.GetFloat() / -100.0f;
				GL_Viewport( 0, 0, glConfig.vidWidth * shadowResolution * r_fboResolution.GetFloat(), glConfig.vidHeight * shadowResolution * r_fboResolution.GetFloat() );
				FB_ApplyScissor();
			} else {
				if( primaryOn ) {
					GL_Viewport( 0, 0, glConfig.vidWidth * r_fboResolution.GetFloat(), glConfig.vidHeight * r_fboResolution.GetFloat() );
				} else {
					GL_Viewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
				}
				FB_ApplyScissor();
			}
		} else {
			shadowResolution = 1.0f;
		}
	}
	GL_CheckErrors();

	// additional steps for shadowmaps
	if ( r_shadows.GetInteger() == 2 ) {
		qglDepthMask( on );
		//GL_Cull( on ? CT_BACK_SIDED : CT_FRONT_SIDED ); // shadow acne fix, requires includeBackFaces in R_CreateLightTris
		if ( on ) {
			int mipmap = ShadowFboIndex / MAX_SHADOW_MAPS;
			int mapSize = r_shadowMapSize.GetInteger() >> mipmap;
			/*ShadowMipMap[ShadowFboIndex] = 0;
			int lightScreenSize = idMath::Imax( backEnd.vLight->scissorRect.GetWidth(), backEnd.vLight->scissorRect.GetHeight() ),
			         ScreenSize = idMath::Imin( glConfig.vidWidth, glConfig.vidHeight );

			while ( lightScreenSize < screenSize && ShadowMipMap[ShadowFboIndex] < 5 ) {
				ShadowMipMap[ShadowFboIndex]++; // select a smaller map for small/distant lights
				lightScreenSize <<= 1;
				mapSize >>= 1;
			}
			qglFramebufferTexture( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, globalImages->shadowCubeMap[ShadowFboIndex]->texnum, ShadowMipMap[ShadowFboIndex] );*/
			qglViewport( 0, 0, mapSize, mapSize );

			if ( r_useScissor.GetBool() ) {
				GL_Scissor( 0, 0, mapSize, mapSize );
			}

			if ( clear ) {
				qglClear( GL_DEPTH_BUFFER_BIT );
			}
			GL_State( GLS_DEPTHFUNC_LESS ); // reset in RB_GLSL_CreateDrawInteractions
		} else {
			const idScreenRect &r = backEnd.viewDef->viewport;

			GL_Viewport( r.x1, r.y1, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1 );

			if ( r_useScissor.GetBool() ) {
				GL_Scissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				            backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				            backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				            backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
			}
		}
	}
}

void FB_Clear() {
	fboPrimary = fboResolve = fboPostProcess = pbo = 0;
	fboShadows.clear();
	renderBufferColor = renderBufferDepthStencil = renderBufferPostProcess = 0;
}

void EnterPrimary() {
	if ( r_softShadowsQuality.GetBool() ) {
		r_useGLSL.SetBool( true );
	}
	depthCopiedThisView = false;

	if ( !r_useFbo.GetBool() ) {
		return;
	}

	if ( primaryOn ) {
		return;
	}
	CheckCreatePrimary();

	qglBindFramebuffer( GL_FRAMEBUFFER, fboPrimary );

	GL_Viewport( tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,	// FIXME: must not use tr in backend
	             tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
	             backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
	             backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

	GL_Scissor( tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
	            tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
	            backEnd.viewDef->scissor.x2 + 1 - backEnd.viewDef->scissor.x1,
	            backEnd.viewDef->scissor.y2 + 1 - backEnd.viewDef->scissor.y1 );

	qglClear( GL_COLOR_BUFFER_BIT ); // otherwise transparent skybox blends with previous frame

	primaryOn = true;

	GL_CheckErrors();
}

// switch from fbo to default framebuffer, copy content
void LeavePrimary() {
	if ( !primaryOn ) {
		return;
	}
	GL_CheckErrors();

	GL_Viewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	GL_Scissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );

	if ( r_multiSamples.GetInteger() > 1 ) {
		FB_ResolveMultisampling( GL_COLOR_BUFFER_BIT );
		qglBindFramebuffer( GL_READ_FRAMEBUFFER, fboResolve );
	}
	Framebuffer::BindPrimary( GL_DRAW_FRAMEBUFFER );
	qglBlitFramebuffer( 0, 0, globalImages->currentRenderImage->uploadWidth,
	                    globalImages->currentRenderImage->uploadHeight,
	                    0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR );

	if ( r_fboDebug.GetInteger() != 0 ) {
		if ( r_multiSamples.GetInteger() > 1 ) {
			FB_ResolveMultisampling( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		}
		Framebuffer::BindPrimary();

		qglLoadIdentity();
		qglMatrixMode( GL_PROJECTION );
		qglPushMatrix();
		qglLoadIdentity();
		qglOrtho( 0, 1, 0, 1, -1, 1 );

		GL_State( GLS_DEFAULT );
		qglDisable( GL_DEPTH_TEST );
		qglColor3f( 1, 1, 1 );

		while ( 1 )	{
			switch ( r_fboDebug.GetInteger() ) {
			case 2:
				globalImages->currentDepthImage->Bind();
				break;
			case 3:
				globalImages->shadowDepthFbo->Bind();
				qglTexParameteri( GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_DEPTH_COMPONENT );
				break;
			default:
				globalImages->currentRenderImage->Bind();
			}
			RB_DrawFullScreenQuad();
		}
		qglEnable( GL_DEPTH_TEST );
		qglPopMatrix();
		qglMatrixMode( GL_MODELVIEW );
		GL_SelectTexture( 0 );
	}
	Framebuffer::BindPrimary();

	primaryOn = false;

	if ( r_frontBuffer.GetBool() ) {
		qglFinish();
	}
	GL_CheckErrors();
}

void FB_TogglePrimary( bool on ) {
	if ( on ) {
		EnterPrimary();
	} else {
		LeavePrimary();
	}
}
