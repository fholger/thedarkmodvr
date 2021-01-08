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

#include <openxr/openxr.h>
#include "OpenXRSwapchain.h"
#include "OpenXRInput.h"
#include "VRBackend.h"

class OpenXRBackend : public VRBackend {
public:

	void PrepareFrame() override;

	XrInstance Instance() const { return instance; }
	XrSession Session() const { return session; }

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
	bool UsesSrgbTextures() const override;

private:
	XrInstance instance = nullptr;
	XrSystemId system = 0;
	XrSession session = nullptr;
	OpenXRInput input;
	int64_t swapchainFormat = 0;
	XrViewConfigurationView views[2];
	bool vrSessionActive = false;
	bool usingD3D11 = false;

	OpenXRSwapchain* eyeSwapchains[2];
	OpenXRSwapchain* uiSwapchain;

	XrSpace seatedSpace = nullptr;

	XrTime predictedFrameDisplayTime;
	XrDuration displayPeriod;
	XrTime nextFrameDisplayTime;

	XrView renderViews[2];
	XrView predictedViews[2];

	XrDebugUtilsMessengerEXT debugMessenger = nullptr;

	void SetupDebugMessenger();
	void ChooseSwapchainFormat();
	void InitSwapchains();
	void HandleSessionStateChange( const XrEventDataSessionStateChanged &stateChangedEvent );
};

extern OpenXRBackend *xrBackend;
