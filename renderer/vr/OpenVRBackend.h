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
#include "openvr.h"

extern idCVar vr_enable;

class FrameBuffer;

class OpenVRBackend {
public:
	void Init();
	void Shutdown();
	bool IsInitialized() const;

	void BeginFrame();
	void EndFrame();

private:
	vr::IVRSystem *vrSystem = nullptr;
	vr::VROverlayHandle_t menuOverlay;
	vr::TrackedDevicePose_t trackedDevicePose[vr::k_unMaxTrackedDeviceCount];
	vr::TrackedDevicePose_t predictedDevicePose[vr::k_unMaxTrackedDeviceCount];
	float rawProjection[2][4];
	float fovX;
	float fovY;
	float aspect;
	float eyeForward;
	idVec3 hmdOrigin;
	idMat3 hmdAxis;
	idVec3 predictedHmdOrigin;
	idMat3 predictedHmdAxis;
	float scale;
	uint32_t renderWidth;
	uint32_t renderHeight;

	FrameBuffer *eyeFBOs[2] = { nullptr, nullptr };
	idImage *eyeTextures[2] = { nullptr, nullptr };
	FrameBuffer *overlayFBO = nullptr;
	idImage *overlayTexture = nullptr;

	void InitParameters();
	void InitRenderTargets();
	void DetermineRenderTargetSize();

	void CreateEyeFBO( FrameBuffer *fbo, idImage *texture );
};

extern OpenVRBackend *vrBackend;
