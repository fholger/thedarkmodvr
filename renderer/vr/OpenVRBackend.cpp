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

void OpenVRBackend::SubmitFrame() {
	GL_PROFILE("VrSubmitFrame")
	vr::Texture_t texture = { (void*)uiTexture->texnum, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
	vr::VROverlay()->SetOverlayTexture( menuOverlay, &texture );

	GL_ViewportAbsolute( 0, 0, eyeTextures[0]->uploadWidth, eyeTextures[0]->uploadHeight );
	GL_ScissorAbsolute( 0, 0, eyeTextures[0]->uploadWidth, eyeTextures[0]->uploadHeight );
	vr::VRTextureWithPose_t leftEyeTexture, rightEyeTexture;
	leftEyeTexture.mDeviceToAbsoluteTracking = currentPoses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
	leftEyeTexture.eColorSpace = vr::ColorSpace_Gamma;
	leftEyeTexture.eType = vr::TextureType_OpenGL;
	leftEyeTexture.handle = (void*)eyeTextures[0]->texnum;
	rightEyeTexture.mDeviceToAbsoluteTracking = currentPoses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
	rightEyeTexture.eColorSpace = vr::ColorSpace_Gamma;
	rightEyeTexture.eType = vr::TextureType_OpenGL;
	rightEyeTexture.handle = (void*)eyeTextures[1]->texnum;
	vr::VRCompositor()->Submit( vr::Eye_Left, &leftEyeTexture, nullptr, vr::Submit_TextureWithPose );
	vr::VRCompositor()->Submit( vr::Eye_Right, &rightEyeTexture, nullptr, vr::Submit_TextureWithPose );
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

void OpenVRBackend::GetFov( int eye, float &angleLeft, float &angleRight, float &angleUp, float &angleDown ) {
	angleLeft = projectionFov[eye][0];
	angleRight = projectionFov[eye][1];
	angleUp = projectionFov[eye][3];
	angleDown = projectionFov[eye][2];
}

void OpenVRBackend::InitParameters() {
	float rawProjection[2][4];
	system->GetProjectionRaw( vr::Eye_Left, &rawProjection[0][0], &rawProjection[0][1], &rawProjection[0][2], &rawProjection[0][3] );
	common->Printf( "OpenVR left eye raw projection - l: %.2f r: %.2f t: %.2f b: %.2f\n", rawProjection[0][0], rawProjection[0][1], rawProjection[0][2], rawProjection[0][3] );
	system->GetProjectionRaw( vr::Eye_Right, &rawProjection[1][0], &rawProjection[1][1], &rawProjection[1][2], &rawProjection[1][3] );
	common->Printf( "OpenVR right eye raw projection - l: %.2f r: %.2f t: %.2f b: %.2f\n", rawProjection[1][0], rawProjection[1][1], rawProjection[1][2], rawProjection[1][3] );
	for ( int i = 0; i < 4; ++i ) {
		projectionFov[0][i] = idMath::ATan(rawProjection[0][i]);
		projectionFov[1][i] = idMath::ATan(rawProjection[1][i]);
	}
	float combinedTanHalfFovHoriz = Max( Max( idMath::Fabs(rawProjection[0][0]), idMath::Fabs(rawProjection[0][1]) ), Max( idMath::Fabs(rawProjection[1][0]), idMath::Fabs(rawProjection[1][1]) ) );
	float combinedTanHalfFovVert = Max( Max( idMath::Fabs(rawProjection[0][2]), idMath::Fabs(rawProjection[0][3]) ), Max( idMath::Fabs(rawProjection[1][2]), idMath::Fabs(rawProjection[1][3]) ) );

	fovX = RAD2DEG( 2 * atanf( combinedTanHalfFovHoriz ) ) + 8;
	fovY = RAD2DEG( 2 * atanf( combinedTanHalfFovVert ) ) + 8;

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

void OpenVRBackend::UpdateScissorRect( idScreenRect * scissorRect, viewDef_t * viewDef, const idMat4 &invProj, const idMat4 &invView ) const {
	/*const float renderWidth = eyeBuffers[0]->Width();
	const float renderHeight = eyeBuffers[0]->Height();

	auto ToNdc = [=](idVec4 &vec) {
		vec.x = 2 * vec.x / renderWidth - 1;
		vec.y = 2 * vec.y / renderHeight - 1;
		vec.z = 2 * vec.z - 1;
	};

	idVec4 corners[8];
	corners[0].Set( scissorRect->x1, scissorRect->y1, scissorRect->zmin, 1 );
	corners[0].Set( scissorRect->x2, scissorRect->y1, scissorRect->zmin, 1 );
	corners[0].Set( scissorRect->x1, scissorRect->y2, scissorRect->zmin, 1 );
	corners[0].Set( scissorRect->x2, scissorRect->y2, scissorRect->zmin, 1 );
	corners[0].Set( scissorRect->x1, scissorRect->y1, scissorRect->zmax, 1 );
	corners[0].Set( scissorRect->x2, scissorRect->y1, scissorRect->zmax, 1 );
	corners[0].Set( scissorRect->x1, scissorRect->y2, scissorRect->zmax, 1 );
	corners[0].Set( scissorRect->x2, scissorRect->y2, scissorRect->zmax, 1 );

	for ( int i = 0; i < 8; ++i ) {
		ToNdc( corners[i] );
	}*/
}

extern idScreenRect R_CalcLightScissorRectangle( viewLight_t *vLight, viewDef_t *viewDef );
extern void R_SetupViewFrustum( viewDef_t *viewDef );

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

	idMat4 prevInvProj, prevInvView;
	memcpy( prevInvProj.ToFloatPtr(), viewDef->projectionMatrix, sizeof(idMat4) );
	memcpy( prevInvView.ToFloatPtr(), viewDef->worldSpace.modelViewMatrix, sizeof(idMat4) );
	prevInvProj.InverseSelf();
	prevInvView.InverseSelf();

	// update with new pose
	renderView_t& eyeView = viewDef->renderView;
	eyeView.viewaxis = axis * eyeView.initialViewaxis;
	const int eyeFactor[] = {-1, 1};
	if ( !eyeView.fixedOrigin ) {
		eyeView.vieworg = eyeView.initialVieworg + position * eyeView.initialViewaxis;
		eyeView.vieworg -= eyeFactor[eye] * halfEyeSeparationWorldUnits * eyeView.viewaxis[1];
	}
}

void OpenVRBackend::AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture ) {
	if ( eye == UI ) {
		fbo = uiBuffer;
		texture = uiTexture;
	} else {
		fbo = eyeBuffers[eye];
		texture = eyeTextures[eye];
	}
}

float OpenVRBackend::GetInterPupillaryDistance() const {
	return system->GetFloatTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float );
}
