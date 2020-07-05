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
#include "VrSwapchain.h"

class VrBackend {
public:
	void Init();
	void Destroy();

	void BeginFrame();
	void EndFrame();

	XrInstance Instance() const { return instance; }
	XrSession Session() const { return session; }

	void AdjustRenderView( renderView_t *view );
	void RenderStereoView( const emptyCommand_t * cmds );

private:
	XrInstance instance = nullptr;
	XrSystemId system = 0;
	XrSession session = nullptr;
	GLuint swapchainFormat = 0;
	XrViewConfigurationView views[2];
	bool vrSessionActive = false;
	bool shouldSubmitFrame = false;

	VrSwapchain eyeSwapchains[2];
	VrSwapchain uiSwapchain;

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

extern VrBackend *vr;
