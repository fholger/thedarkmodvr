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

void OpenVRBackend::InitBackend() {
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
	transform.m[1][3] = -0.25;
	transform.m[2][3] = -2.5;
	vr::VROverlay()->SetOverlayWidthInMeters( menuOverlay, 3 );
	vr::VROverlay()->SetOverlayTexelAspect( menuOverlay, 1.5f );
	vr::VROverlay()->SetOverlayTransformAbsolute( menuOverlay, vr::TrackingUniverseSeated, &transform );
	vr::VROverlay()->ShowOverlay( menuOverlay );

	InitParameters();
	InitRenderTextures();
	common->Printf( "-----------------------------\n" );
}

void OpenVRBackend::DestroyBackend() {
	if ( system != nullptr ) {
		common->Printf( "Shutting down OpenVR.\n" );
		vr::VR_Shutdown();
		system = nullptr;
	}	
}

void OpenVRBackend::PrepareFrame() {
	// we are only predicting the next poses for the frontend here
	float secondsSinceLastVsync;
	system->GetTimeSinceLastVsync( &secondsSinceLastVsync, nullptr );
	float displayFrequency = system->GetFloatTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float );
	float frameDuration = 1.f / displayFrequency;
	float vsyncToPhotons = system->GetFloatTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float );
	// predict 2 frames in advance - while backend renders current frame, frontend calculates the next one
	float predictedFrameTime = 2 * frameDuration - secondsSinceLastVsync + vsyncToPhotons;

	system->GetDeviceToAbsoluteTrackingPose( vr::TrackingUniverseSeated, predictedFrameTime, predictedPoses, vr::k_unMaxTrackedDeviceCount );
}

bool OpenVRBackend::BeginFrame() {
	GL_PROFILE("WaitGetPoses")
	vr::VRCompositor()->WaitGetPoses( currentPoses, vr::k_unMaxTrackedDeviceCount, nullptr, 0 );
	return true;
}

void OpenVRBackend::SubmitFrame() {
	GL_PROFILE("VrSubmitFrame")
	vr::Texture_t texture = { (void*)uiTexture->texnum, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
	// note: the Linux SteamVR implementation seems to adhere to glViewport/glScissor when applying the overlay texture, so ensure they are correct
	qglViewport( 0, 0, uiTexture->uploadWidth, uiTexture->uploadHeight );
	qglScissor( 0, 0, uiTexture->uploadWidth, uiTexture->uploadHeight );
	vr::VROverlay()->SetOverlayTexture( menuOverlay, &texture );
	vr::HmdMatrix34_t transform;
	memset( transform.m, 0, sizeof( transform.m ) );
	transform.m[0][0] = 1;
	transform.m[1][1] = 1;
	transform.m[2][2] = 1;
	transform.m[1][3] = vr_uiOverlayVerticalOffset.GetFloat();
	transform.m[2][3] = -vr_uiOverlayDistance.GetFloat();
	vr::VROverlay()->SetOverlayWidthInMeters( menuOverlay, vr_uiOverlayAspect.GetFloat() * vr_uiOverlayHeight.GetFloat() );
	vr::VROverlay()->SetOverlayTexelAspect( menuOverlay, vr_uiOverlayAspect.GetFloat() );
	vr::VROverlay()->SetOverlayTransformAbsolute( menuOverlay, vr::TrackingUniverseSeated, &transform );

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
	int flags = vr::Submit_TextureWithPose | ( frameDiscontinuity ? vr::Submit_FrameDiscontinuty : 0 );
	vr::VRCompositor()->Submit( vr::Eye_Left, &leftEyeTexture, nullptr, (vr::EVRSubmitFlags)flags );
	vr::VRCompositor()->Submit( vr::Eye_Right, &rightEyeTexture, nullptr, (vr::EVRSubmitFlags)flags );
	vr::VRCompositor()->PostPresentHandoff();
}

void OpenVRBackend::GetFov( int eye, float &angleLeft, float &angleRight, float &angleUp, float &angleDown ) {
	angleLeft = projectionFov[eye][0];
	angleRight = projectionFov[eye][1];
	angleUp = -projectionFov[eye][2];
	angleDown = -projectionFov[eye][3];
}

bool OpenVRBackend::GetCurrentEyePose( int eye, idVec3 &origin, idQuat &orientation ) {
	const vr::TrackedDevicePose_t &hmdPose = currentPoses[vr::k_unTrackedDeviceIndex_Hmd];
	if ( !hmdPose.bPoseIsValid ) {
		return false;
	}
	CalcEyePose( hmdPose.mDeviceToAbsoluteTracking, eye, origin, orientation );
	return true;
}

bool OpenVRBackend::GetPredictedEyePose( int eye, idVec3 &origin, idQuat &orientation ) {
	const vr::TrackedDevicePose_t &hmdPose = predictedPoses[vr::k_unTrackedDeviceIndex_Hmd];
	if ( !hmdPose.bPoseIsValid ) {
		return false;
	}
	CalcEyePose( hmdPose.mDeviceToAbsoluteTracking, eye, origin, orientation );
	return true;
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

void OpenVRBackend::AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture ) {
	if ( eye == UI ) {
		fbo = uiBuffer;
		texture = uiTexture;
	} else {
		fbo = eyeBuffers[eye];
		texture = eyeTextures[eye];
	}
}

idList<idVec2> OpenVRBackend::GetHiddenAreaMask( eyeView_t eye ) {
	vr::HiddenAreaMesh_t hiddenAreaMesh = system->GetHiddenAreaMesh( eye == LEFT_EYE ? vr::Eye_Left : vr::Eye_Right );
	idList<idVec2> vertices;
	uint32_t numVertices = hiddenAreaMesh.unTriangleCount * 3;
	vertices.SetNum( numVertices );
	for ( uint32_t i = 0; i < numVertices; ++i ) {
		vertices[i].Set( -1 + 2 * hiddenAreaMesh.pVertexData[i].v[0], -1 + 2 * hiddenAreaMesh.pVertexData[i].v[1] );
	}
	return vertices;	
}

idVec4 OpenVRBackend::GetVisibleAreaBounds( eyeView_t eye ) {
	vr::HiddenAreaMesh_t visibleAreaMesh = system->GetHiddenAreaMesh( eye == LEFT_EYE ? vr::Eye_Left : vr::Eye_Right, vr::k_eHiddenAreaMesh_Inverse );
	idVec4 bounds ( 1, 1, 0, 0 );
	uint32_t numVertices = visibleAreaMesh.unTriangleCount * 3;
	for ( uint32_t i = 0; i < numVertices; ++i ) {
		const float *v = visibleAreaMesh.pVertexData[i].v;
		bounds[0] = Min( bounds[0], v[0] );
		bounds[1] = Min( bounds[1], v[1] );
		bounds[2] = Max( bounds[2], v[0] );
		bounds[3] = Max( bounds[3], v[1] );
	}
	return bounds;
}

void OpenVRBackend::CalcEyePose( const vr::HmdMatrix34_t &headPose, int eye, idVec3 &origin, idQuat &orientation ) {
	idVec3 viewPos ( -MetresToGameUnits * headPose.m[2][3], -MetresToGameUnits * headPose.m[0][3], MetresToGameUnits * headPose.m[1][3] );
	idMat3 viewRot;
	viewRot[0].Set( headPose.m[2][2], headPose.m[0][2], -headPose.m[1][2] );
	viewRot[1].Set( headPose.m[2][0], headPose.m[0][0], -headPose.m[1][0] );
	viewRot[2].Set( -headPose.m[2][1], -headPose.m[0][1], headPose.m[1][1] );

	vr::HmdMatrix34_t eyeMat = system->GetEyeToHeadTransform( eye == LEFT_EYE ? vr::Eye_Left : vr::Eye_Right );
	idVec3 eyePos ( -MetresToGameUnits * eyeMat.m[2][3], -MetresToGameUnits * eyeMat.m[0][3], MetresToGameUnits * eyeMat.m[1][3] );
	idMat3 eyeRot;
	eyeRot[0].Set( eyeMat.m[2][2], eyeMat.m[0][2], -eyeMat.m[1][2] );
	eyeRot[1].Set( eyeMat.m[2][0], eyeMat.m[0][0], -eyeMat.m[1][0] );
	eyeRot[2].Set( -eyeMat.m[2][1], -eyeMat.m[0][1], eyeMat.m[1][1] );

	orientation = (eyeRot * viewRot).ToQuat();
	origin = viewPos + eyePos * viewRot;
}
