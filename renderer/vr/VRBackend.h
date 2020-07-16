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

extern idCVar vr_lockMousePitch;
extern idCVar vr_uiOverlayHeight;
extern idCVar vr_uiOverlayAspect;
extern idCVar vr_uiOverlayDistance;
extern idCVar vr_uiOverlayVerticalOffset;
extern idCVar vr_waitPosition;

class VRBackend {
public:
	void Init();
	void Destroy();

	virtual void GetFrontendPoses() = 0;
 
	virtual void AdjustRenderView( renderView_t *view ) = 0;
	void RenderStereoView( const emptyCommand_t * cmds );
	void DrawHiddenAreaMeshToDepth();

	void UpdateLightScissor( viewLight_t *vLight );

protected:
	virtual void InitBackend() = 0;
	virtual void DestroyBackend() = 0;
	
	enum eyeView_t {
		LEFT_EYE = 0,
		RIGHT_EYE = 1,
		UI = 2,
	};
	void ExecuteRenderCommands( const emptyCommand_t *cmds, eyeView_t eyeView );
	virtual void AwaitFrame() = 0;
	virtual void SubmitFrame() = 0;
	virtual void GetFov( int eye, float &angleLeft, float &angleRight, float &angleUp, float &angleDown ) = 0;
	virtual bool GetCurrentEyePose( int eye, idVec3 &origin, idMat3 &axis ) = 0;
	virtual void AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture ) = 0;

	virtual idList<idVec2> GetHiddenAreaMask( eyeView_t eye ) = 0;

private:
	void InitHiddenAreaMesh();
	void UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye );
	void SetupProjectionMatrix( viewDef_t *viewDef, int eye );
	void UpdateViewPose( viewDef_t *viewDef, int eye );
	void MirrorVrView( idImage *eyeTexture, idImage *uiTexture );

	GLSLProgram *vrMirrorShader = nullptr;
	GLuint hiddenAreaMeshBuffer = 0;
	GLuint numVertsLeft = 0;
	GLuint numVertsRight = 0;
	GLSLProgram *hiddenAreaMeshShader = nullptr;
	eyeView_t currentEye;
};

extern VRBackend *vrBackend;
extern idCVar vr_uiResolution;

void SelectVRImplementation();
