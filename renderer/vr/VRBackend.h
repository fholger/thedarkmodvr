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
#include "../tr_local.h"

class FrameBuffer;
class GLSLProgram;

extern idCVar vr_decoupleMouseMovement;
extern idCVar vr_decoupledMouseYawAngle;
extern idCVar vr_uiOverlayHeight;
extern idCVar vr_uiOverlayAspect;
extern idCVar vr_uiOverlayDistance;
extern idCVar vr_uiOverlayVerticalOffset;
extern idCVar vr_force2DRender;
extern idCVar vr_disableUITransparency;
extern idCVar vr_aimIndicatorSize;
extern idCVar vr_aimIndicatorRangedSize;
extern idCVar vr_aimIndicatorRangedMultiplier;
extern idCVar vr_disableZoomAnimations;

class VRBackend {
public:
	void Init();
	void Destroy();

	virtual void PrepareFrame() = 0;
 
	void AdjustRenderView( renderView_t *view );
	void RenderStereoView( const frameData_t *frameData );
	void DrawHiddenAreaMeshToDepth();
	void DrawRadialDensityMaskToDepth();

	void UpdateLightScissor( viewLight_t *vLight );

	bool UseRadialDensityMask();

	static const float GameUnitsToMetres;
	static const float MetresToGameUnits;
protected:
	virtual void InitBackend() = 0;
	virtual void DestroyBackend() = 0;
	
	enum eyeView_t {
		LEFT_EYE = 0,
		RIGHT_EYE = 1,
		UI = 2,
	};
	void ExecuteRenderCommands( const frameData_t *frameData, eyeView_t eyeView );
	virtual bool BeginFrame() = 0;
	virtual void SubmitFrame() = 0;
	virtual void GetFov( int eye, float &angleLeft, float &angleRight, float &angleUp, float &angleDown ) = 0;
	virtual bool GetPredictedEyePose( int eye, idVec3 &origin, idQuat &orientation ) = 0;
	virtual bool GetCurrentEyePose( int eye, idVec3 &origin, idQuat &orientation ) = 0;
	virtual void AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture ) = 0;

	virtual idList<idVec2> GetHiddenAreaMask( eyeView_t eye ) = 0;
	virtual idVec4 GetVisibleAreaBounds( eyeView_t eye ) = 0;

	virtual bool UsesSrgbTextures() const = 0;
	bool frameDiscontinuity = false;

private:
	void InitHiddenAreaMesh();
	void UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye );
	void SetupProjectionMatrix( viewDef_t *viewDef, int eye );
	void UpdateViewPose( viewDef_t *viewDef, int eye );
	void MirrorVrView( idImage *eyeTexture, idImage *uiTexture );
	void DrawAimIndicator( float size );
	void UpdateFrameStatus( const frameData_t *frameData );
	void DrawComfortVignette(eyeView_t eye);
	void PrepareVariableRateShading();
	void ReconstructImageFromRdm();

	eyeView_t currentEye;
	GLSLProgram *vrMirrorShader = nullptr;

	GLuint hiddenAreaMeshBuffer = 0;
	GLuint numVertsLeft = 0;
	GLuint numVertsRight = 0;
	GLSLProgram *hiddenAreaMeshShader = nullptr;
	GLSLProgram *radialDensityMaskShader = nullptr;
	GLSLProgram *rdmReconstructShader = nullptr;
	idVec4 visibleAreaBounds[2];

	idVec3 aimIndicatorPos;
	idMat4 aimIndicatorMvp;
	GLSLProgram *vrAimIndicatorShader = nullptr;

	GLSLProgram *comfortVignetteShader = nullptr;
	bool vignetteEnabled = false;
	idVec3 lastCameraPosition;
	idAngles lastCameraAngles;
	uint64_t lastCameraUpdateTime = 0;

	idImage *variableRateShadingImage = nullptr;
	idImage *rdmReconstructionImage = nullptr;
	FrameBuffer *rdmReconstructionFbo = nullptr;
};

extern VRBackend *vrBackend;
extern idCVar vr_uiResolution;

void SelectVRImplementation();
