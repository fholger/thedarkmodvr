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
	void Init() override;
	void Destroy() override;

	void BeginFrame() override;
	void EndFrame() override;

	void AdjustRenderView( renderView_t *view ) override;
	void RenderStereoView( const emptyCommand_t * cmds ) override;

private:
	vr::IVRSystem *system = nullptr;
	vr::VROverlayHandle_t menuOverlay = 0;
	vr::TrackedDevicePose_t currentPoses[vr::k_unMaxTrackedDeviceCount];
	vr::TrackedDevicePose_t predictedPoses[vr::k_unMaxTrackedDeviceCount];
	float rawProjection[2][4];
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

	void ExecuteRenderCommands( const emptyCommand_t *cmds, bool render3D, bool renderLightgem );
	void UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye );

	void SetupProjectionMatrix( viewDef_t *viewDef, int eye );
	void UpdateScissorRect( idScreenRect *scissorRect, viewDef_t *viewDef, const idMat4 &invProj,
	                        const idMat4 &invView ) const;
	void UpdateViewPose( viewDef_t *viewDef, int eye );

	float GetInterPupillaryDistance() const;
};

extern OpenVRBackend *openvr;
