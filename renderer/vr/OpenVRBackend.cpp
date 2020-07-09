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
#include "OpenVRBackend.h"
#include <openvr/openvr.h>

#include "../tr_local.h"
#include "../FrameBuffer.h"
#include "../FrameBufferManager.h"
#include "../Profiling.h"
#include "../backend/RenderBackend.h"

OpenVRBackend openvrImpl;
OpenVRBackend *openvr = &openvrImpl;

void OpenVRBackend::Init() {
	common->Printf( "-----------------------------\n" );
	common->Printf( "Initializing OpenVR...\n");
	
	if ( !vr::VR_IsHmdPresent() ) {
		common->FatalError( "No OpenVR HMD detected" );
	}

	vr::EVRInitError initError = vr::VRInitError_None;
	system = vr::VR_Init( &initError, vr::VRApplication_Scene );
	if ( initError != vr::VRInitError_None ) {
		common->FatalError( "OpenVR initialization failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription( initError ) );
	}
	
	vr::VRCompositor()->SetTrackingSpace( vr::TrackingUniverseSeated );

	vr::EVROverlayError overlayError = vr::VROverlay()->CreateOverlay( "tdm_ui_overlay", "The Dark Mod UI", &menuOverlay );
	if ( overlayError != vr::VROverlayError_None ) {
		common->FatalError( "OpenVR overlay initialization failed: %d", overlayError );
	}
	vr::HmdMatrix34_t transform;
	memset( transform.m, 0, sizeof( transform.m ) );
	transform.m[0][0] = 1;
	transform.m[1][1] = 1;
	transform.m[2][2] = 1;
	transform.m[2][3] = -2;
	vr::VROverlay()->SetOverlayWidthInMeters( menuOverlay, 2 );
	vr::VROverlay()->SetOverlayTexelAspect( menuOverlay, 1.5f );
	vr::VROverlay()->SetOverlayTransformAbsolute( menuOverlay, vr::TrackingUniverseSeated, &transform );
	vr::VROverlay()->ShowOverlay( menuOverlay );

	InitParameters();
	InitRenderTextures();
	common->Printf( "-----------------------------\n" );
}

void OpenVRBackend::Destroy() {
	if ( system != nullptr ) {
		common->Printf( "Shutting down OpenVR.\n" );
		vr::VR_Shutdown();
		system = nullptr;
	}	
}

void OpenVRBackend::BeginFrame() {
	GL_PROFILE("WaitGetPoses")
	vr::VRCompositor()->WaitGetPoses( currentPoses, vr::k_unMaxTrackedDeviceCount, predictedPoses, vr::k_unMaxTrackedDeviceCount );	
}

void OpenVRBackend::EndFrame() {
	GL_PROFILE("VrSubmitFrame")
	vr::Texture_t texture = { (void*)uiTexture->texnum, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
	vr::VROverlay()->SetOverlayTexture( menuOverlay, &texture );

	GL_ViewportAbsolute( 0, 0, eyeTextures[0]->uploadWidth, eyeTextures[0]->uploadHeight );
	GL_ScissorAbsolute( 0, 0, eyeTextures[0]->uploadWidth, eyeTextures[0]->uploadHeight );
	vr::Texture_t leftEyeTexture = { (void*)eyeTextures[0]->texnum, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
	vr::Texture_t rightEyeTexture = { (void*)eyeTextures[1]->texnum, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
	vr::VRCompositor()->Submit( vr::Eye_Left, &leftEyeTexture );
	vr::VRCompositor()->Submit( vr::Eye_Right, &rightEyeTexture );
	vr::VRCompositor()->PostPresentHandoff();
}

void OpenVRBackend::AdjustRenderView( renderView_t *view ) {
	const vr::TrackedDevicePose_t &hmdPose = predictedPoses[vr::k_unTrackedDeviceIndex_Hmd];
	if ( !hmdPose.bPoseIsValid ) {
		return;
	}

	const float scale = 1.0f / 0.02309f;

	const vr::HmdMatrix34_t &mat = hmdPose.mDeviceToAbsoluteTracking;
	idVec3 position ( -scale * mat.m[2][3], -scale * mat.m[0][3], scale * mat.m[1][3] );
	idMat3 axis;
	axis[0].Set( mat.m[2][2], mat.m[0][2], -mat.m[1][2] );
	axis[1].Set( mat.m[2][0], mat.m[0][0], -mat.m[1][0] );
	axis[2].Set( -mat.m[2][1], -mat.m[0][1], mat.m[1][1] );
	position += eyeForward * scale * axis[0];

	view->initialViewaxis = view->viewaxis;
	view->initialVieworg = view->vieworg;

	view->vieworg += position * view->viewaxis;
	view->viewaxis = axis * view->viewaxis;
}

void OpenVRBackend::RenderStereoView( const emptyCommand_t *cmds ) {
	FrameBuffer *defaultFbo = frameBuffers->defaultFbo;

	// render lightgem and 2D UI elements
	frameBuffers->defaultFbo = uiBuffer;
	frameBuffers->defaultFbo->Bind();
	qglClearColor( 0, 0, 0, 0 );
	qglClear( GL_COLOR_BUFFER_BIT );
	ExecuteRenderCommands( cmds, false );

	// render stereo views
	for ( int eye = 0; eye < 2; ++eye ) {
		UpdateRenderViewsForEye( cmds, eye );
		frameBuffers->defaultFbo = eyeBuffers[eye];
		frameBuffers->defaultFbo->Bind();
		qglClearColor( 0, 0, 0, 1 );
		qglClear( GL_COLOR_BUFFER_BIT );
		frameBuffers->EnterPrimary();
		ExecuteRenderCommands( cmds, true );
		frameBuffers->LeavePrimary();
		FB_DebugShowContents();
	}

	eyeBuffers[0]->BlitTo( defaultFbo, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	uiBuffer->BlitTo( defaultFbo, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	EndFrame();
	GLimp_SwapBuffers();
	frameBuffers->defaultFbo = defaultFbo;
	// go back to the default texture so the editor doesn't mess up a bound image
	qglBindTexture( GL_TEXTURE_2D, 0 );
	backEnd.glState.tmu[0].current2DMap = -1;
	
}

void OpenVRBackend::InitParameters() {
	system->GetProjectionRaw( vr::Eye_Left, &rawProjection[0][0], &rawProjection[0][1], &rawProjection[0][2], &rawProjection[0][3] );
	common->Printf( "OpenVR left eye raw projection - l: %.2f r: %.2f t: %.2f b: %.2f\n", rawProjection[0][0], rawProjection[0][1], rawProjection[0][2], rawProjection[0][3] );
	system->GetProjectionRaw( vr::Eye_Right, &rawProjection[1][0], &rawProjection[1][1], &rawProjection[1][2], &rawProjection[1][3] );
	common->Printf( "OpenVR right eye raw projection - l: %.2f r: %.2f t: %.2f b: %.2f\n", rawProjection[1][0], rawProjection[1][1], rawProjection[1][2], rawProjection[1][3] );
	float combinedTanHalfFovHoriz = Max( Max( rawProjection[0][0], rawProjection[0][1] ), Max( rawProjection[1][0], rawProjection[1][1] ) );
	float combinedTanHalfFovVert = Max( Max( rawProjection[0][2], rawProjection[0][3] ), Max( rawProjection[1][2], rawProjection[1][3] ) );

	fovX = RAD2DEG( 2 * atanf( combinedTanHalfFovHoriz ) ) + 5;
	fovY = RAD2DEG( 2 * atanf( combinedTanHalfFovVert ) ) + 5;

	vr::HmdMatrix34_t eyeToHead = system->GetEyeToHeadTransform( vr::Eye_Right );
	eyeForward = -eyeToHead.m[2][3];
	common->Printf( "Distance from eye to head: %.2f m\n", eyeForward );
}

void OpenVRBackend::InitRenderTextures() {
	uint32_t width, height;
	system->GetRecommendedRenderTargetSize( &width, &height );
	common->Printf( "Recommended render target size %d x %d\n", width, height );

	// hack: force primary fbo to use our desired render resolution
	glConfig.vidWidth = width;
	glConfig.vidHeight = height;
	r_fboResolution.SetModified();

	uiTexture = globalImages->ImageFromFunction( "vr_ui", FB_RenderTexture );
	eyeTextures[0] = globalImages->ImageFromFunction( "vr_left", FB_RenderTexture );
	eyeTextures[1] = globalImages->ImageFromFunction( "vr_right", FB_RenderTexture );
	uiBuffer = frameBuffers->CreateFromGenerator( "vr_ui", [=](FrameBuffer *fbo) {
		CreateFrameBuffer( fbo, uiTexture, vr_uiResolution.GetInteger(), vr_uiResolution.GetInteger() );
	} );
	eyeBuffers[0] = frameBuffers->CreateFromGenerator( "vr_left", [=](FrameBuffer *fbo) {
		CreateFrameBuffer( fbo, eyeTextures[0], width, height );
	} );
	eyeBuffers[1] = frameBuffers->CreateFromGenerator( "vr_right", [=](FrameBuffer *fbo) {
		CreateFrameBuffer( fbo, eyeTextures[1], width, height );
	} );
}

void OpenVRBackend::CreateFrameBuffer( FrameBuffer *fbo, idImage *texture, uint32_t width, uint32_t height ) {
	texture->GenerateAttachment( width, height, GL_RGBA8, GL_LINEAR );
	fbo->Init( width, height );
	fbo->AddColorRenderTexture( 0, texture );
}

extern void RB_Tonemap( bloomCommand_t *cmd );
extern void RB_CopyRender( const void *data );

void OpenVRBackend::ExecuteRenderCommands( const emptyCommand_t *cmds, bool render3D ) {
	RB_SetDefaultGLState();

	bool isv3d = false, fboOff = false; // needs to be declared outside of switch case

	while ( cmds ) {
		switch ( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW: {
			backEnd.viewDef = ( ( const drawSurfsCommand_t * )cmds )->viewDef;
			isv3d = ( backEnd.viewDef->viewEntitys != nullptr );	// view is 2d or 3d
			if ( (backEnd.viewDef->IsLightGem() && !render3D) || (!backEnd.viewDef->IsLightGem() && isv3d == render3D) ) {
				renderBackend->DrawView( backEnd.viewDef );
			}
			break;
		}
		case RC_SET_BUFFER:
			// not applicable
			break;
		case RC_BLOOM:
			if ( !render3D ) {
				break;
			}
			RB_Tonemap( (bloomCommand_t*)cmds );
			FB_DebugShowContents();
			fboOff = true;
			break;
		case RC_COPY_RENDER:
			if ( !render3D ) {
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
}

void OpenVRBackend::UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye ) {
	GL_PROFILE("UpdateRenderViewsForEye")
	
	for ( const emptyCommand_t *cmd = cmds; cmd; cmd = ( const emptyCommand_t * )cmd->next ) {
		if ( cmd->commandId == RC_DRAW_VIEW ) {
			viewDef_t *viewDef = ( ( const drawSurfsCommand_t * )cmd )->viewDef;
			if ( viewDef->IsLightGem() || viewDef->viewEntitys == nullptr ) {
				continue;
			}

			SetupProjectionMatrix( viewDef, eye );
			UpdateViewPose( viewDef, eye );
		}
	}
}

void OpenVRBackend::SetupProjectionMatrix( viewDef_t *viewDef, int eye ) {
	const float zNear = (viewDef->renderView.cramZNear) ? (r_znear.GetFloat() * 0.25f) : r_znear.GetFloat();
	const float idx = 1.0f / (rawProjection[eye][1] - rawProjection[eye][0]);
	const float idy = 1.0f / (rawProjection[eye][3] - rawProjection[eye][2]);
	const float sx = rawProjection[eye][0] + rawProjection[eye][1];
	const float sy = rawProjection[eye][2] + rawProjection[eye][3];

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

void OpenVRBackend::UpdateViewPose( viewDef_t *viewDef, int eye ) {
	const vr::TrackedDevicePose_t &hmdPose = predictedPoses[vr::k_unTrackedDeviceIndex_Hmd];
	if ( !hmdPose.bPoseIsValid ) {
		return;
	}

	const float scale = 1.0f / 0.02309f;

	const vr::HmdMatrix34_t &mat = hmdPose.mDeviceToAbsoluteTracking;
	idVec3 position ( -scale * mat.m[2][3], -scale * mat.m[0][3], scale * mat.m[1][3] );
	idMat3 axis;
	axis[0].Set( mat.m[2][2], mat.m[0][2], -mat.m[1][2] );
	axis[1].Set( mat.m[2][0], mat.m[0][0], -mat.m[1][0] );
	axis[2].Set( -mat.m[2][1], -mat.m[0][1], mat.m[1][1] );
	position += eyeForward * scale * axis[0];

	float halfEyeSeparationWorldUnits = 0.5f * GetInterPupillaryDistance() * scale;

	// update with new pose
	renderView_t& eyeView = viewDef->renderView;
	eyeView.viewaxis = axis * eyeView.initialViewaxis;
	const int eyeFactor[] = {-1, 1};
	if ( !eyeView.fixedOrigin ) {
		eyeView.vieworg = eyeView.initialVieworg + position * eyeView.initialViewaxis;
		eyeView.vieworg -= eyeFactor[eye] * halfEyeSeparationWorldUnits * eyeView.viewaxis[1];
	}

	// apply new view matrix to view and all entities
	R_SetViewMatrix( *viewDef );
	for ( viewEntity_t *vEntity = viewDef->viewEntitys; vEntity; vEntity = vEntity->next ) {
		myGlMultMatrix( vEntity->modelMatrix, viewDef->worldSpace.modelViewMatrix, vEntity->modelViewMatrix );
	}

	for ( viewLight_t *vLight = viewDef->viewLights; vLight; vLight = vLight->next ) {
		//vLight->scissorRect = R_CalcLightScissorRect( vLight );
	}
}

float OpenVRBackend::GetInterPupillaryDistance() const {
	return system->GetFloatTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float );
}
