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
#include "VRBackend.h"
#include "OpenXRBackend.h"
#include "OpenVRBackend.h"
#include "../FrameBuffer.h"
#include "../FrameBufferManager.h"
#include "../Profiling.h"
#include "../backend/RenderBackend.h"

VRBackend *vrBackend = nullptr;

idCVar vr_backend( "vr_backend", "0", CVAR_RENDERER|CVAR_INTEGER|CVAR_ARCHIVE, "0 - OpenVR, 1 - OpenXR" );
idCVar vr_uiResolution( "vr_uiResolution", "2048", CVAR_RENDERER|CVAR_ARCHIVE|CVAR_INTEGER, "Render resolution for 2D/UI overlay" );

extern void RB_Tonemap( bloomCommand_t *cmd );
extern void RB_CopyRender( const void *data );

void VRBackend::RenderStereoView( const emptyCommand_t *cmds ) {
	FrameBuffer *defaultFbo = frameBuffers->defaultFbo;

	FrameBuffer *uiBuffer;
	FrameBuffer *eyeBuffers[2];
	idImage *uiTexture;
	idImage *eyeTextures[2];
	AcquireFboAndTexture( UI, uiBuffer, uiTexture );
	AcquireFboAndTexture( LEFT_EYE, eyeBuffers[0], eyeTextures[0] );
	AcquireFboAndTexture( RIGHT_EYE, eyeBuffers[1], eyeTextures[1] );

	// render stereo views
	for ( int eye = 0; eye < 2; ++eye ) {
		frameBuffers->defaultFbo = eyeBuffers[eye];
		frameBuffers->defaultFbo->Bind();
		qglClearColor( 0, 0, 0, 1 );
		qglClear( GL_COLOR_BUFFER_BIT );
		ExecuteRenderCommands( cmds, (eyeView_t)eye );
		FB_DebugShowContents();
	}

	// render 2D UI elements
	frameBuffers->defaultFbo = uiBuffer;
	frameBuffers->defaultFbo->Bind();
	GL_ViewportRelative( 0, 0, 1, 1 );
	GL_ScissorRelative( 0, 0, 1, 1 );
	qglClearColor( 0, 0, 0, 0 );
	qglClear( GL_COLOR_BUFFER_BIT );
	ExecuteRenderCommands( cmds, UI );

	eyeBuffers[0]->BlitTo( defaultFbo, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	uiBuffer->BlitTo( defaultFbo, GL_COLOR_BUFFER_BIT, GL_LINEAR );

	SubmitFrame();
	GLimp_SwapBuffers();

	frameBuffers->defaultFbo = defaultFbo;
	// go back to the default texture so the editor doesn't mess up a bound image
	qglBindTexture( GL_TEXTURE_2D, 0 );
	backEnd.glState.tmu[0].current2DMap = -1;
}

void VRBackend::ExecuteRenderCommands( const emptyCommand_t *cmds, eyeView_t eyeView ) {
	if ( eyeView != UI ) {
		UpdateRenderViewsForEye( cmds, eyeView );
	}
	
	RB_SetDefaultGLState();

	bool isv3d = false; // needs to be declared outside of switch case

	while ( cmds ) {
		switch ( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW: {
			backEnd.viewDef = ( ( const drawSurfsCommand_t * )cmds )->viewDef;
			isv3d = ( backEnd.viewDef->viewEntitys != nullptr );	// view is 2d or 3d
			if ( (backEnd.viewDef->IsLightGem() && eyeView == LEFT_EYE) || (!backEnd.viewDef->IsLightGem() && isv3d == (eyeView != UI) ) ) {
				if ( eyeView != UI ) {
					frameBuffers->EnterPrimary();
				}
				renderBackend->DrawView( backEnd.viewDef );
			}
			break;
		}
		case RC_SET_BUFFER:
			// not applicable
			break;
		case RC_BLOOM:
			if ( eyeView == UI ) {
				break;
			}
			RB_Tonemap( (bloomCommand_t*)cmds );
			FB_DebugShowContents();
			break;
		case RC_COPY_RENDER:
			if ( eyeView == UI ) {
				break;
			}
			RB_CopyRender( cmds );
			break;
		case RC_SWAP_BUFFERS:
			// not applicable
			break;
		default:
			common->Error( "VRBackend::ExecuteRenderCommands: bad commandId" );
			break;
		}
		cmds = ( const emptyCommand_t * )cmds->next;
	}

	frameBuffers->LeavePrimary();
}

extern idScreenRect R_CalcLightScissorRectangle( viewLight_t *vLight, viewDef_t *viewDef );
extern void R_SetupViewFrustum( viewDef_t *viewDef );

void VRBackend::UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye ) {
	GL_PROFILE("UpdateRenderViewsForEye")
	
	std::vector<viewDef_t *> views;
	for ( const emptyCommand_t *cmd = cmds; cmd; cmd = ( const emptyCommand_t * )cmd->next ) {
		if ( cmd->commandId == RC_DRAW_VIEW ) {
			viewDef_t *viewDef = ( ( const drawSurfsCommand_t * )cmd )->viewDef;
			if ( viewDef->IsLightGem() || viewDef->viewEntitys == nullptr ) {
				continue;
			}

			views.push_back( viewDef );
		}
	}

	// process views in reverse, since we need to update the main view first before any (mirrored) subviews
	for ( auto it = views.rbegin(); it != views.rend(); ++it ) {
		viewDef_t *viewDef = *it;

		SetupProjectionMatrix( viewDef, eye );
		UpdateViewPose( viewDef, eye );

		// apply new view matrix to view and all entities
		R_SetViewMatrix( *viewDef );
		R_SetupViewFrustum( viewDef );
		for ( viewEntity_t *vEntity = viewDef->viewEntitys; vEntity; vEntity = vEntity->next ) {
			myGlMultMatrix( vEntity->modelMatrix, viewDef->worldSpace.modelViewMatrix, vEntity->modelViewMatrix );
		}

		for ( viewLight_t *vLight = viewDef->viewLights; vLight; vLight = vLight->next ) {
			vLight->scissorRect = R_CalcLightScissorRectangle( vLight, viewDef );
		}
	}
}

void VRBackend::SetupProjectionMatrix( viewDef_t *viewDef, int eye ) {
	const float zNear = (viewDef->renderView.cramZNear) ? (r_znear.GetFloat() * 0.25f) : r_znear.GetFloat();
	float angleLeft, angleRight, angleUp, angleDown;
	GetFov( eye, angleLeft, angleRight, angleUp, angleDown );
	const float idx = 1.0f / (tan(angleRight) - tan(angleLeft));
	const float idy = 1.0f / (tan(angleUp) - tan(angleDown));
	const float sx = tan(angleRight) + tan(angleLeft);
	const float sy = tan(angleDown) + tan(angleUp);

	viewDef->projectionMatrix[0 * 4 + 0] = 2.0f * idx;
	viewDef->projectionMatrix[1 * 4 + 0] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 0] = sx * idx;
	viewDef->projectionMatrix[3 * 4 + 0] = 0.0f;

	viewDef->projectionMatrix[0 * 4 + 1] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 1] = 2.0f * idy;
	viewDef->projectionMatrix[2 * 4 + 1] = sy*idy;	
	viewDef->projectionMatrix[3 * 4 + 1] = 0.0f;

	viewDef->projectionMatrix[0 * 4 + 2] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 2] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 2] = -0.999f; 
	viewDef->projectionMatrix[3 * 4 + 2] = -2.0f * zNear;

	viewDef->projectionMatrix[0 * 4 + 3] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 3] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 3] = -1.0f;
	viewDef->projectionMatrix[3 * 4 + 3] = 0.0f;
}

extern void R_MirrorPoint( const idVec3 in, orientation_t *surface, orientation_t *camera, idVec3 &out );
extern void R_MirrorVector( const idVec3 in, orientation_t *surface, orientation_t *camera, idVec3 &out );

void VRBackend::UpdateViewPose( viewDef_t *viewDef, int eye ) {
	idVec3 position;
	idMat3 axis;
	if ( !GetCurrentEyePose( eye, position, axis ) ) {
		return;
	}

	// update with new pose
	if ( viewDef->isMirror ) {
		assert( viewDef->superView != nullptr );
		renderView_t *origRv = nullptr;
		// find the nearest mirror info stored in this or parent views
		for ( viewDef_t *vd = viewDef; vd; vd = vd->superView ) {
			if ( vd->renderView.isMirror ) {
				origRv = &vd->renderView;
				break;
			}
		}

		if ( origRv == nullptr ) {
			common->Warning( "Failed to find mirror info, not adjusting view..." );
			return;
		}

		if ( origRv == &viewDef->renderView ) {
			// update mirror axis and origin
			idMat3 origaxis = axis * origRv->initialViewaxis;
			idVec3 origpos = origRv->initialVieworg + position * origRv->initialViewaxis;
			// set the mirrored origin and axis
			R_MirrorPoint( origpos, &origRv->mirrorSurface, &origRv->mirrorCamera, viewDef->renderView.vieworg );

			R_MirrorVector( origaxis[0], &origRv->mirrorSurface, &origRv->mirrorCamera, viewDef->renderView.viewaxis[0] );
			R_MirrorVector( origaxis[1], &origRv->mirrorSurface, &origRv->mirrorCamera, viewDef->renderView.viewaxis[1] );
			R_MirrorVector( origaxis[2], &origRv->mirrorSurface, &origRv->mirrorCamera, viewDef->renderView.viewaxis[2] );
		} else {
			// recalculate mirrored axis from parent mirror view
			idMat3 mirroredAxis = origRv->viewaxis * origRv->initialViewaxis.Inverse() * viewDef->renderView.initialViewaxis;
			if ( !viewDef->renderView.fixedOrigin ) {
				idVec3 mirroredPos = viewDef->renderView.initialVieworg + position * viewDef->renderView.initialViewaxis;
			}
		}

		renderView_t &parentRv = viewDef->superView->renderView;
		renderView_t &eyeView = viewDef->renderView;
	} else {
		renderView_t& eyeView = viewDef->renderView;
		eyeView.viewaxis = axis * eyeView.initialViewaxis;
		if ( !eyeView.fixedOrigin ) {
			eyeView.vieworg = eyeView.initialVieworg + position * eyeView.initialViewaxis;
		}
	}
}

void SelectVRImplementation() {
	if ( vr_backend.GetInteger() == 0 ) {
		vrBackend = openvr;
	} else {
		vrBackend = xrBackend;	
	}
}
