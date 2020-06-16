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

idCVar vr_enable( "vr_enable", "1", CVAR_RENDERER | CVAR_BOOL, "enable OpenVR rendering" );

OpenVRBackend openVrBackend;
OpenVRBackend *vrBackend = &openVrBackend;

void OpenVRBackend::Init() {
	if( !vr_enable.GetBool() ) {
		return;		
	}

	common->Printf( "Initializing OpenVR support...\n" );
	if (!vr::VR_IsHmdPresent())
	{
		common->Printf("No OpenVR HMD detected.\n");
		return;
	}

	vr::EVRInitError initError = vr::VRInitError_None;
	vrSystem = vr::VR_Init(&initError, vr::VRApplication_Scene);
	if (initError != vr::VRInitError_None)
	{
		vrSystem = nullptr;
		common->Warning("OpenVR initialization failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(initError));
		return;
	}

	vr::VRCompositor()->SetTrackingSpace( vr::TrackingUniverseSeated );
	vr::EVROverlayError overlayError = vr::VROverlay()->CreateOverlay( "tdm_2d_overlay", "The Dark Mod 2D overlay", &menuOverlay );
	if (overlayError != vr::VROverlayError_None) {
		common->Warning( "OpenVR overlay initialization failed: %d", overlayError );
		Shutdown();
		return;
	}
	InitParameters();
	common->Printf( "OpenVR support ready.\n" );
}

void OpenVRBackend::Shutdown() {}

bool OpenVRBackend::IsInitialized() const {
	return vrSystem != nullptr;
}

void OpenVRBackend::InitParameters() {
	vrSystem->GetProjectionRaw( vr::Eye_Left, &rawProjection[0][0], &rawProjection[0][1], &rawProjection[0][2], &rawProjection[0][3] );
	common->Printf( "OpenVR left eye raw projection - l: %.2f r: %.2f t: %.2f b: %.2f\n", rawProjection[0][0], rawProjection[0][1], rawProjection[0][2], rawProjection[0][3] );
	vrSystem->GetProjectionRaw( vr::Eye_Right, &rawProjection[1][0], &rawProjection[1][1], &rawProjection[1][2], &rawProjection[1][3] );
	common->Printf( "OpenVR right eye raw projection - l: %.2f r: %.2f t: %.2f b: %.2f\n", rawProjection[1][0], rawProjection[1][1], rawProjection[1][2], rawProjection[1][3] );
	float combinedTanHalfFovHoriz = std::max( std::max( rawProjection[0][0], rawProjection[0][1] ), std::max( rawProjection[1][0], rawProjection[1][1] ) );
	float combinedTanHalfFovVert = std::max( std::max( rawProjection[0][2], rawProjection[0][3] ), std::max( rawProjection[1][2], rawProjection[1][3] ) );

	fovX = RAD2DEG( 2 * atanf( combinedTanHalfFovHoriz ) );
	fovY = RAD2DEG( 2 * atanf( combinedTanHalfFovVert ) );
	aspect = combinedTanHalfFovHoriz / combinedTanHalfFovVert;
	common->Printf( "OpenVR field of view x: %.1f  y: %.1f  Aspect: %.2f\n", fovX, fovY, aspect );

	scale = 1 / 0.02309f; // meters to world units (1.1 inch)

	vr::HmdMatrix34_t eyeToHead = vrSystem->GetEyeToHeadTransform( vr::Eye_Right );
	eyeForward = -eyeToHead.m[2][3];
	common->Printf( "Distance from eye to head: %.2f m\n", eyeForward );

	hmdOrigin.Zero();
	hmdAxis.Identity();
	predictedHmdOrigin.Zero();
	predictedHmdAxis.Identity();	
}
