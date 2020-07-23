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

class VRBackend {
public:
	void Init();
	void Destroy();

	virtual void PrepareFrame() = 0;
 
	virtual void AdjustRenderView( renderView_t *view ) = 0;
	void RenderStereoView( const frameData_t *frameData );
	void DrawHiddenAreaMeshToDepth();

	void UpdateLightScissor( viewLight_t *vLight );

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
	virtual bool GetCurrentEyePose( int eye, idVec3 &origin, idMat3 &axis ) = 0;
	virtual void AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture ) = 0;
	virtual float GetHalfEyeDistance() const = 0;

	virtual idList<idVec2> GetHiddenAreaMask( eyeView_t eye ) = 0;

	virtual bool UsesSrgbTextures() const = 0;

private:
	void InitHiddenAreaMesh();
	void UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye );
	void SetupProjectionMatrix( viewDef_t *viewDef, int eye );
	void UpdateViewPose( viewDef_t *viewDef, int eye );
	void MirrorVrView( idImage *eyeTexture, idImage *uiTexture );
	void DrawAimIndicator();
	void UpdateComfortVignetteStatus( const frameData_t *frameData );
	void DrawComfortVignette(eyeView_t eye);

	GLSLProgram *vrMirrorShader = nullptr;
	GLuint hiddenAreaMeshBuffer = 0;
	GLuint numVertsLeft = 0;
	GLuint numVertsRight = 0;
	GLSLProgram *hiddenAreaMeshShader = nullptr;
	eyeView_t currentEye;

	idVec3 aimIndicatorPos;
	idMat4 aimIndicatorMvp;
	GLSLProgram *vrAimIndicatorShader = nullptr;

	GLSLProgram *comfortVignetteShader = nullptr;
	bool vignetteEnabled = false;
	idVec3 lastCameraPosition;
	idAngles lastCameraAngles;
	uint64_t lastCameraUpdateTime = 0;
};

extern VRBackend *vrBackend;
extern idCVar vr_uiResolution;

void SelectVRImplementation();
