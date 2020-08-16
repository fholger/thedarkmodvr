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
#pragma once
#include "VRBackend.h"
#include "../tr_local.h"
#include <openvr/openvr.h>

class FrameBuffer;

class OpenVRBackend : public VRBackend {
public:
	void PrepareFrame() override;

protected:
	void InitBackend() override;
	void DestroyBackend() override;

	bool BeginFrame() override;
	void SubmitFrame() override;
	void GetFov( int eye, float &angleLeft, float &angleRight, float &angleUp, float &angleDown ) override;
	bool GetCurrentEyePose( int eye, idVec3 &origin, idQuat &orientation ) override;
	bool GetPredictedEyePose( int eye, idVec3 &origin, idQuat &orientation ) override;
	void AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture ) override;
	idList<idVec2> GetHiddenAreaMask( eyeView_t eye ) override;
	idVec4 GetVisibleAreaBounds( eyeView_t eye ) override;
	bool UsesSrgbTextures() const override { return false; }

private:
	vr::IVRSystem *system = nullptr;
	vr::VROverlayHandle_t menuOverlay = 0;
	vr::TrackedDevicePose_t currentPoses[vr::k_unMaxTrackedDeviceCount];
	vr::TrackedDevicePose_t predictedPoses[vr::k_unMaxTrackedDeviceCount];
	float projectionFov[2][4];
	float fovX;
	float fovY;
	float eyeForward;

	idImage *eyeTextures[2];
	FrameBuffer *eyeBuffers[2];
	idImage *uiTexture;
	FrameBuffer *uiBuffer;

	void InitParameters();
	void InitRenderTextures();

	void CreateFrameBuffer( FrameBuffer *fbo, idImage *texture, uint32_t width, uint32_t height );

	void CalcEyePose( const vr::HmdMatrix34_t &headPose, int eye, idVec3 &origin, idQuat &orientation );
};

extern OpenVRBackend *openvr;
