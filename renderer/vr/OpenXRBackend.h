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
#include "../tr_local.h"
#include "GamepadInput.h"
#include "OpenXRInput.h"
#include "VRFoveatedRendering.h"

extern idCVar vr_uiOverlayHeight;
extern idCVar vr_uiOverlayAspect;
extern idCVar vr_uiOverlayDistance;
extern idCVar vr_uiOverlayVerticalOffset;
extern idCVar vr_aimIndicatorSize;
extern idCVar vr_aimIndicatorRangedMultiplier;
extern idCVar vr_aimIndicatorRangedSize;
extern idCVar vr_decoupleMouseMovement;
extern idCVar vr_decoupledMouseYawAngle;
extern idCVar vr_force2DRender;
extern idCVar vr_disableZoomAnimations;

class OpenXRBackend {
public:
	enum eyeView_t {
		LEFT_EYE = 0,
		RIGHT_EYE = 1,
		UI = 2,
	};

	void Init();
	void Destroy();

	void PrepareFrame();
 
	void AdjustRenderView( renderView_t *view );
	void RenderStereoView( const frameData_t *frameData );
	void DrawHiddenAreaMeshToDepth();
	void DrawRadialDensityMaskToDepth();

	void UpdateLightScissor( viewLight_t *vLight );

	bool UseRadialDensityMask();

	void UpdateInput(int axis[6], idList<padActionChange_t> &actionChanges, poseInput_t &poseInput);

	idVec2 ProjectCenterUV( int eye );

	static const float GameUnitsToMetres;
	static const float MetresToGameUnits;

	XrInstance Instance() const { return instance; }
	XrSession Session() const { return session; }

private:
	void InitBackend();
	void DestroyBackend();

	void ExecuteRenderCommands( const frameData_t *frameData, eyeView_t eyeView );
	bool BeginFrame();
	void SubmitFrame();
	void GetFov( int eye, float &angleLeft, float &angleRight, float &angleUp, float &angleDown );
	bool GetCurrentEyePose( int eye, idVec3 &origin, idQuat &orientation );
	bool GetPredictedEyePose( int eye, idVec3 &origin, idQuat &orientation );
	void AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture );
	idList<idVec2> GetHiddenAreaMask( eyeView_t eye );
	idVec4 GetVisibleAreaBounds( eyeView_t eye );
	bool UsesSrgbTextures() const;

	void InitHiddenAreaMesh();
	void UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye );
	void SetupProjectionMatrix( viewDef_t *viewDef, int eye );
	void UpdateViewPose( viewDef_t *viewDef, int eye );
	void MirrorVrView( idImage *eyeTexture, idImage *uiTexture );
	void DrawAimIndicator( float size );
	void UpdateFrameStatus( const frameData_t *frameData );
	void DrawComfortVignette(eyeView_t eye);

	eyeView_t currentEye;
	GLSLProgram *vrMirrorShader = nullptr;

	GLuint hiddenAreaMeshBuffer = 0;
	GLuint numVertsLeft = 0;
	GLuint numVertsRight = 0;
	GLSLProgram *hiddenAreaMeshShader = nullptr;
	idVec4 visibleAreaBounds[2];

	idVec3 aimIndicatorPos;
	idMat4 aimIndicatorMvp;
	GLSLProgram *vrAimIndicatorShader = nullptr;

	GLSLProgram *comfortVignetteShader = nullptr;
	bool vignetteEnabled = false;
	idVec3 lastCameraPosition;
	idAngles lastCameraAngles;
	uint64_t lastCameraUpdateTime = 0;

	VRFoveatedRendering foveatedHelper;

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

extern OpenXRBackend *vrBackend;
