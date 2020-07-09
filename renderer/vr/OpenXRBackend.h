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
#include "VRBackend.h"

class OpenXRBackend : public VRBackend {
public:
	void Init() override;
	void Destroy() override;

	void BeginFrame() override;
	void SubmitFrame() override;

	XrInstance Instance() const { return instance; }
	XrSession Session() const { return session; }

	void AdjustRenderView( renderView_t *view ) override;
	void RenderStereoView( const emptyCommand_t * cmds ) override;

private:
	XrInstance instance = nullptr;
	XrSystemId system = 0;
	XrSession session = nullptr;
	GLuint swapchainFormat = 0;
	XrViewConfigurationView views[2];
	bool vrSessionActive = false;
	bool shouldSubmitFrame = false;

	OpenXRSwapchain eyeSwapchains[2];
	OpenXRSwapchain uiSwapchain;

	XrSpace seatedSpace = nullptr;

	XrTime currentFrameDisplayTime;
	XrTime nextFrameDisplayTime;

	XrView renderViews[2];

	XrDebugUtilsMessengerEXT debugMessenger = nullptr;

	void SetupDebugMessenger();
	void ChooseSwapchainFormat();
	void InitSwapchains();
	void HandleSessionStateChange( const XrEventDataSessionStateChanged &stateChangedEvent );

	void ExecuteRenderCommands( const emptyCommand_t *cmds, bool render3D );
	void UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye );
};

extern OpenXRBackend *xrBackend;