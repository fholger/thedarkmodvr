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
#include "precompiled.h"
#include "OpenXRBackend.h"


#include "OpenXRSwapchainGL.h"
#include "xr_loader.h"
#include "xr_math.h"
#include "../tr_local.h"
#include "../FrameBuffer.h"
#include "../FrameBufferManager.h"
#include "../GLSLProgramManager.h"
#include "../GLSLUniforms.h"
#include "../Profiling.h"
#include "../backend/RenderBackend.h"
#include "../qgl.h"

OpenXRBackend xrImpl;
OpenXRBackend *vrBackend = &xrImpl;

idCVar xr_preferD3D11 ( "xr_preferD3D11", "1", CVAR_RENDERER|CVAR_BOOL|CVAR_ARCHIVE, "Use D3D11 for OpenXR session to work around a performance issue with SteamVR's OpenXR implementation" );
idCVar vr_uiResolution( "vr_uiResolution", "2048", CVAR_RENDERER|CVAR_ARCHIVE|CVAR_INTEGER, "Render resolution for 2D/UI overlay" );
idCVar vr_decoupleMouseMovement( "vr_decoupleMouseMovement", "1", CVAR_ARCHIVE|CVAR_BOOL, "If enabled, vertical mouse movement will not be reflected in the VR view" );
idCVar vr_decoupledMouseYawAngle( "vr_decoupledMouseYawAngle", "15", CVAR_RENDERER|CVAR_FLOAT|CVAR_ARCHIVE, "horizontal mouse movement within this angle is decoupled from the view rotation" );
idCVar vr_useHiddenAreaMesh( "vr_useHiddenAreaMesh", "1", CVAR_RENDERER|CVAR_BOOL|CVAR_ARCHIVE, "If enabled, renders a hidden area mesh to depth to save on pixel rendering cost for pixels that can't be seen in the HMD" );
idCVar vr_uiOverlayHeight( "vr_uiOverlayHeight", "2", CVAR_RENDERER|CVAR_FLOAT|CVAR_ARCHIVE, "Height of the UI/HUD overlay in metres" );
idCVar vr_uiOverlayAspect( "vr_uiOverlayAspect", "1.5", CVAR_RENDERER|CVAR_FLOAT|CVAR_ARCHIVE, "Aspect ratio of the UI overlay" );
idCVar vr_uiOverlayDistance( "vr_uiOverlayDistance", "2.5", CVAR_RENDERER|CVAR_FLOAT|CVAR_ARCHIVE, "Distance in metres from the player position at which the UI overlay is positioned" );
idCVar vr_uiOverlayVerticalOffset( "vr_uiOverlayVerticalOffset", "-0.5", CVAR_RENDERER|CVAR_FLOAT|CVAR_ARCHIVE, "Vertical offset in metres of the UI overlay's position" );
idCVar vr_aimIndicator("vr_aimIndicator", "1", CVAR_BOOL|CVAR_RENDERER|CVAR_ARCHIVE, "Display an aim indicator to show where the mouse is currently aiming at");
idCVar vr_aimIndicatorSize("vr_aimIndicatorSize", "0.5", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Size of the mouse aim indicator");
idCVar vr_aimIndicatorColorR("vr_aimIndicatorColorR", "0.5", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Red component of the mouse aim indicator color");
idCVar vr_aimIndicatorColorG("vr_aimIndicatorColorG", "0.5", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Green component of the mouse aim indicator color");
idCVar vr_aimIndicatorColorB("vr_aimIndicatorColorB", "0.5", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Blue component of the mouse aim indicator color");
idCVar vr_aimIndicatorColorA("vr_aimIndicatorColorA", "1", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Alpha component of the mouse aim indicator color");
idCVar vr_aimIndicatorRangedMultiplier("vr_aimIndicatorRangedMultiplier", "4", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "A multiplier for the maximum depth the aim indicator can reach when using the bow");
idCVar vr_aimIndicatorRangedSize("vr_aimIndicatorRangedSize", "1.5", CVAR_FLOAT|CVAR_RENDERER|CVAR_ARCHIVE, "Size of the aim indicator when using a ranged weapon");
idCVar vr_force2DRender("vr_force2DRender", "0", CVAR_RENDERER|CVAR_INTEGER, "Force rendering to the 2D overlay instead of stereo");
idCVar vr_disableUITransparency("vr_disableUITransparency", "1", CVAR_RENDERER|CVAR_BOOL|CVAR_ARCHIVE, "Disable transparency when rendering UI elements (may have unintended side-effects)");
idCVar vr_comfortVignette("vr_comfortVignette", "0", CVAR_RENDERER|CVAR_BOOL|CVAR_ARCHIVE, "Enable a vignette effect on artificial movement");
idCVar vr_comfortVignetteRadius("vr_comfortVignetteRadius", "0.6", CVAR_RENDERER|CVAR_FLOAT|CVAR_ARCHIVE, "The radius/size of the comfort vignette" );
idCVar vr_comfortVignetteCorridor("vr_comfortVignetteCorridor", "0.1", CVAR_RENDERER|CVAR_FLOAT|CVAR_ARCHIVE, "Transition corridor width from black to visible of the comfort vignette" );
idCVar vr_disableZoomAnimations("vr_disableZoomAnimations", "0", CVAR_RENDERER|CVAR_BOOL|CVAR_ARCHIVE, "If set to 1, any zoom effect will be instant without transitioning animation");
idCVar vr_useLightScissors("vr_useLightScissors", "1", CVAR_RENDERER|CVAR_BOOL, "Use individual scissor rects per light (helps performance, might lead to incorrect shadows in border cases)");

extern void RB_Tonemap();
extern void RB_Bloom( bloomCommand_t *cmd );
extern void RB_CopyRender( const void *data );

const float OpenXRBackend::GameUnitsToMetres = 0.02309f;
const float OpenXRBackend::MetresToGameUnits = 1.0f / GameUnitsToMetres;

void OpenXRBackend::Init() {
	InitBackend();
	InitHiddenAreaMesh();

	vrMirrorShader = programManager->LoadFromFiles( "vr_mirror", "vr/vr_mirror.vert.glsl", "vr/vr_mirror.frag.glsl" );
	vrAimIndicatorShader = programManager->LoadFromFiles( "aim_indicator", "vr/aim_indicator.vert.glsl", "vr/aim_indicator.frag.glsl" );
	comfortVignetteShader = programManager->LoadFromFiles( "comfort_vignette", "vr/comfort_vignette.vert.glsl", "vr/comfort_vignette.frag.glsl" );

	foveatedHelper.Init();
}

void OpenXRBackend::Destroy() {
	vrAimIndicatorShader = nullptr;
	vrMirrorShader = nullptr;
	qglDeleteBuffers( 1, &hiddenAreaMeshBuffer );
	hiddenAreaMeshBuffer = 0;
	hiddenAreaMeshShader = nullptr;
	DestroyBackend();
}

void OpenXRBackend::AdjustRenderView( renderView_t *view ) {
	idVec3 leftEyePos, rightEyePos;
	idQuat leftEyeRot, rightEyeRot;
	if ( !GetPredictedEyePose( LEFT_EYE, leftEyePos, leftEyeRot ) || !GetPredictedEyePose( RIGHT_EYE, rightEyePos, rightEyeRot ) ) {
		return;
	}

	float fov[2][4];
	GetFov( LEFT_EYE, fov[0][0], fov[0][1], fov[0][2], fov[0][3] );
	GetFov( RIGHT_EYE, fov[1][0], fov[1][1], fov[1][2], fov[1][3] );
	float maxHorizFov = Max( Max( idMath::Fabs(fov[0][0]), idMath::Fabs(fov[0][1])), Max( idMath::Fabs(fov[1][0]), idMath::Fabs(fov[1][1])) );
	float maxVertFov = Max( Max( idMath::Fabs(fov[0][2]), idMath::Fabs(fov[0][3])), Max( idMath::Fabs(fov[1][2]), idMath::Fabs(fov[1][3])) );
	// Some headsets have canted lenses. We determine the angle between the forward vectors (x axes) of the eyes
	// and add that to the horizontal FOV to ensure we are not clipping visible objects
	idMat3 leftEyeMat = leftEyeRot.ToMat3();
	idMat3 rightEyeMat = rightEyeRot.ToMat3();
	idVec3 leftEyeForward = leftEyeMat[0];
	idVec3 rightEyeForward = rightEyeMat[0];
	float cantedAngle = idMath::Fabs( idMath::ACos( leftEyeForward * rightEyeForward ) );
	view->fov_x = RAD2DEG( cantedAngle + 2 * maxHorizFov );
	view->fov_y = RAD2DEG( 2 * maxVertFov );
	// add a bit extra to the FOV since the view may have moved slightly when we actually render,
	// but ensure not to reach 180� or more, as the math breaks down at that point
	view->fov_x = Min( 178.f, view->fov_x + 5 );
	view->fov_y = Min( 178.f, view->fov_y + 5 );

	idQuat viewOrientation = .5f * ( leftEyeRot + rightEyeRot );
	viewOrientation.Normalize();
	idMat3 viewAxis = viewOrientation.ToMat3();
	idVec3 viewOrigin = .5f * ( leftEyePos + rightEyePos );
	// move the origin slightly back to ensure both eye frustums are contained within our combined frustum
	float eyeDistance = ( rightEyePos - leftEyePos ).Length();
	float offset = 0.5f * eyeDistance / idMath::Tan( DEG2RAD( view->fov_x / 2 ) );
	viewOrigin -= viewAxis[0] * offset;

	view->initialViewaxis = view->viewaxis;
	view->initialVieworg = view->vieworg;
	view->fixedOrigin = false;
	view->vieworg += viewOrigin * view->viewaxis;
	view->viewaxis = viewAxis * view->viewaxis;

	view->eyeorg[0] = view->initialVieworg + leftEyePos * view->initialViewaxis;
	view->eyeorg[1] = view->initialVieworg + rightEyePos * view->initialViewaxis;

	// calculate the appropriate near z value for our view origin to contain both eye frustums
	idVec3 leftNearZ = leftEyePos + idMath::Cos( fov[0][0] ) * leftEyeMat[0] - idMath::Sin( fov[0][0] ) * leftEyeMat[1];
	idVec3 rightNearZ = rightEyePos + idMath::Cos( fov[1][1] ) * rightEyeMat[0] - idMath::Sin( fov[1][1] ) * rightEyeMat[1];
	float nearZ = ( .5f * ( leftNearZ + rightNearZ ) - viewOrigin ).Length();
	view->nearZOffset = nearZ - r_znear.GetFloat();
}

void OpenXRBackend::RenderStereoView( const frameData_t *frameData ) {
	if ( !BeginFrame() ) {
		return;
	}

	UpdateFrameStatus( frameData );

	emptyCommand_t *cmds = frameData->cmdHead;
	const_cast<frameData_t *>(frameData)->render2D |= vr_force2DRender.GetBool();

	FrameBuffer *defaultFbo = frameBuffers->defaultFbo;

	FrameBuffer *uiBuffer;
	FrameBuffer *eyeBuffers[2];
	idImage *uiTexture;
	idImage *eyeTextures[2];
	AcquireFboAndTexture( UI, uiBuffer, uiTexture );
	AcquireFboAndTexture( LEFT_EYE, eyeBuffers[0], eyeTextures[0] );
	AcquireFboAndTexture( RIGHT_EYE, eyeBuffers[1], eyeTextures[1] );

#ifdef __linux__
	// hack: current SteamVR OpenXR implementation is broken and does not properly reset the GL context after certain calls
	GLimp_ActivateContext();
#endif

	aimIndicatorPos = frameData->mouseAimPosition;

	// render stereo views
	for ( int eye = 0; eye < 2; ++eye ) {
		foveatedHelper.PrepareVariableRateShading( eye );
		frameBuffers->defaultFbo = eyeBuffers[eye];
		frameBuffers->defaultFbo->Bind();
		GL_ViewportRelative( 0, 0, 1, 1 );
		GL_ScissorRelative( 0, 0, 1, 1 );
		qglClearColor( 0, 0, 0, 1 );
		qglClear( GL_COLOR_BUFFER_BIT );
		ExecuteRenderCommands( frameData, (eyeView_t)eye );
		if ( vr_comfortVignette.GetBool() && vignetteEnabled ) {
			DrawComfortVignette((eyeView_t)eye);
		}
		if ( !frameData->render2D ) {
			DrawAimIndicator( frameData->mouseAimSize );
		}
	}
	foveatedHelper.DisableVariableRateShading();

	// render lightgem and 2D UI elements
	frameBuffers->defaultFbo = uiBuffer;
	frameBuffers->defaultFbo->Bind();
	GL_ViewportRelative( 0, 0, 1, 1 );
	GL_ScissorRelative( 0, 0, 1, 1 );
	qglClearColor( 0, 0, 0, 0 );
	qglClear( GL_COLOR_BUFFER_BIT );
	ExecuteRenderCommands( frameData, UI );

	defaultFbo->Bind();
	MirrorVrView( eyeTextures[0], uiTexture );
	GLimp_SwapBuffers();

	SubmitFrame();

	frameBuffers->defaultFbo = defaultFbo;
	// go back to the default texture so the editor doesn't mess up a bound image
	qglBindTexture( GL_TEXTURE_2D, 0 );
	backEnd.glState.tmu[0].current2DMap = -1;
}

void OpenXRBackend::DrawHiddenAreaMeshToDepth() {
	if ( hiddenAreaMeshBuffer == 0 || currentEye == UI || !vr_useHiddenAreaMesh.GetBool() ) {
		return;
	}

	qglBindBuffer( GL_ARRAY_BUFFER, hiddenAreaMeshBuffer );
	qglVertexAttribPointer( 0, 2, GL_FLOAT, false, sizeof( idVec2 ), 0 );
	hiddenAreaMeshShader->Activate();
	int start = currentEye == LEFT_EYE ? 0 : numVertsLeft;
	int count = currentEye == LEFT_EYE ? numVertsLeft : numVertsRight;
	GL_Cull( CT_TWO_SIDED );
	qglDrawArrays( GL_TRIANGLES, start, count );
	GL_Cull( CT_FRONT_SIDED );
	vertexCache.UnbindVertex();
}

void OpenXRBackend::DrawRadialDensityMaskToDepth() {
	if ( currentEye == UI || !UseRadialDensityMask() ) {
		return;
	}

	foveatedHelper.DrawRadialDensityMaskToDepth( currentEye );
}

void OpenXRBackend::ExecuteRenderCommands( const frameData_t *frameData, eyeView_t eyeView ) {
	if ( eyeView != UI && frameData->render2D ) {
		// we are currently not rendering in stereoscopic mode
		return;
	}

	currentEye = eyeView;

	const emptyCommand_t *cmds = frameData->cmdHead;
	
	if ( eyeView != UI ) {
		UpdateRenderViewsForEye( cmds, eyeView );
	}
	
	RB_SetDefaultGLState();

	bool isv3d = false; // needs to be declared outside of switch case
	bool lastViewWasLightgem = false;

	bool shouldRender3D = ( eyeView != UI || frameData->render2D );
	bool shouldRender2D = eyeView == UI;

	while ( cmds ) {
		switch ( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW: {
			backEnd.viewDef = ( ( const drawSurfsCommand_t * )cmds )->viewDef;
			isv3d = ( backEnd.viewDef->viewEntitys != nullptr && !backEnd.viewDef->renderWorld->mapName.IsEmpty() );
			if ( (isv3d && shouldRender3D) || (!isv3d && shouldRender2D) ) {
				if ( isv3d && shouldRender3D ) {
					frameBuffers->EnterPrimary();
				}
				const_cast<viewDef_t*>(backEnd.viewDef)->updateShadowMap = eyeView == UI || eyeView == LEFT_EYE;
				renderBackend->DrawView( backEnd.viewDef );
			}
			break;
		}
		case RC_DRAW_LIGHTGEM:
			if ( !shouldRender2D ) {
				break;
			}
			backEnd.viewDef = ( ( const drawSurfsCommand_t * )cmds )->viewDef;
			renderBackend->DrawLightgem( backEnd.viewDef, ( ( const drawLightgemCommand_t *)cmds )->dataBuffer );
			break;
		case RC_DRAW_SURF:
			if ( !shouldRender3D ) {
				break;
			}
			extern void RB_DrawSingleSurface( drawSurfCommand_t *cmd );
			RB_DrawSingleSurface( ( drawSurfCommand_t *) cmds );
			break;
		case RC_SET_BUFFER:
			// not applicable
			break;
		case RC_BLOOM:
			if ( !shouldRender3D ) {
				break;
			}
			RB_Bloom( (bloomCommand_t*)cmds );
			FB_DebugShowContents();
			break;
		case RC_COPY_RENDER:
			if ( !shouldRender3D ) {
				break;
			}
			RB_CopyRender( cmds );
			break;
		case RC_SWAP_BUFFERS:
			// not applicable
			break;
		default:
			common->Error( "VRBackend::ExecuteRenderCommands: bad commandId" );
			break;
		}
		cmds = ( const emptyCommand_t * )cmds->next;
	}

	frameBuffers->LeavePrimary();
	if ( shouldRender3D ) {
		if ( UseRadialDensityMask() && currentEye != UI ) {
			foveatedHelper.ReconstructImageFromRdm( currentEye );
		}
		RB_Tonemap();
	}
}

extern void R_SetupViewFrustum( viewDef_t *viewDef );

void OpenXRBackend::InitHiddenAreaMesh() {
	visibleAreaBounds[LEFT_EYE] = GetVisibleAreaBounds( LEFT_EYE );
	visibleAreaBounds[RIGHT_EYE] = GetVisibleAreaBounds( RIGHT_EYE );
	
	idList<idVec2> leftEyeVerts = GetHiddenAreaMask( LEFT_EYE );
	idList<idVec2> rightEyeVerts = GetHiddenAreaMask( RIGHT_EYE );
	if ( leftEyeVerts.Num() == 0 || rightEyeVerts.Num() == 0 ) {
		return;
	}

	numVertsLeft = leftEyeVerts.Num();
	numVertsRight = rightEyeVerts.Num();

	leftEyeVerts.Append( rightEyeVerts );

	qglGenBuffers( 1, &hiddenAreaMeshBuffer );
	qglBindBuffer( GL_ARRAY_BUFFER, hiddenAreaMeshBuffer );
	qglBufferData( GL_ARRAY_BUFFER, leftEyeVerts.Size(), leftEyeVerts.Ptr(), GL_STATIC_DRAW );

	hiddenAreaMeshShader = programManager->LoadFromFiles( "hidden_area_mesh", "vr/depth_mask.vert.glsl", "vr/hidden_area_mesh.frag.glsl" );
}

idScreenRect VR_WorldBoundsToScissor( idBounds bounds, const viewDef_t *view ) {
	idBounds projected;
	idRenderMatrix::ProjectedBounds( projected, view->worldSpace.mvp, bounds );
	
	idScreenRect scissor;
	scissor.Clear();

	if ( projected[0][2] >= projected[1][2] ) {
		// the light was culled to the view frustum
		return scissor;
	}

	float screenWidth = (float)view->viewport.x2 - (float)view->viewport.x1;
	float screenHeight = (float)view->viewport.y2 - (float)view->viewport.y1;

	scissor.x1 = idMath::Ftoi( projected[0][0] * screenWidth );
	scissor.x2 = idMath::Ftoi( projected[1][0] * screenWidth );
	scissor.y1 = idMath::Ftoi( projected[0][1] * screenHeight );
	scissor.y2 = idMath::Ftoi( projected[1][1] * screenHeight );
	scissor.Expand();

	scissor.zmin = projected[0][2];
	scissor.zmax = projected[1][2];
	return scissor;
}

idScreenRect VR_CalcLightScissorRectangle( viewLight_t *vLight, const viewDef_t *viewDef ) {
	// Calculate the matrix that projects the zero-to-one cube to exactly cover the
	// light frustum in clip space.
	idRenderMatrix invProjectMVPMatrix;
	idRenderMatrix::Multiply( viewDef->worldSpace.mvp, vLight->lightDef->inverseBaseLightProject, invProjectMVPMatrix );

	// Calculate the projected bounds
	idBounds projected;
	idRenderMatrix::ProjectedFullyClippedBounds( projected, invProjectMVPMatrix, bounds_zeroOneCube );

	idScreenRect lightScissorRect;
	lightScissorRect.Clear();
	
	if ( projected[0][2] >= projected[1][2] ) {
		// the light was culled to the view frustum
		return lightScissorRect;
	}

	float screenWidth = (float)viewDef->viewport.x2 - (float)viewDef->viewport.x1;
	float screenHeight = (float)viewDef->viewport.y2 - (float)viewDef->viewport.y1;

	lightScissorRect.x1 = idMath::Ftoi( projected[0][0] * screenWidth );
	lightScissorRect.x2 = idMath::Ftoi( projected[1][0] * screenWidth );
	lightScissorRect.y1 = idMath::Ftoi( projected[0][1] * screenHeight );
	lightScissorRect.y2 = idMath::Ftoi( projected[1][1] * screenHeight );
	lightScissorRect.Expand();
	lightScissorRect.Intersect( viewDef->scissor );

	lightScissorRect.zmin = idMath::ClampFloat(0, 1, projected[0][2] - 0.005f);
	lightScissorRect.zmax = idMath::ClampFloat(0, 1, projected[1][2] + 0.005f);
	return lightScissorRect;
}

void OpenXRBackend::UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye ) {
	GL_PROFILE("UpdateRenderViewsForEye")
	
	std::vector<viewDef_t *> views;
	for ( const emptyCommand_t *cmd = cmds; cmd; cmd = ( const emptyCommand_t * )cmd->next ) {
		if ( cmd->commandId == RC_DRAW_VIEW ) {
			viewDef_t *viewDef = ( ( const drawSurfsCommand_t * )cmd )->viewDef;
			if ( viewDef->IsLightGem() || viewDef->viewEntitys == nullptr || viewDef->renderWorld->mapName.IsEmpty() ) {
				continue;
			}

			views.push_back( viewDef );
		}
	}

	// process views in reverse, since we need to update the main view first before any (mirrored) subviews
	for ( auto it = views.rbegin(); it != views.rend(); ++it ) {
		viewDef_t *viewDef = *it;

		if ( eye == LEFT_EYE ) {
			memcpy( viewDef->headView.ToFloatPtr(), viewDef->worldSpace.modelViewMatrix, sizeof(idMat4) );
		}

		SetupProjectionMatrix( viewDef, eye );
		UpdateViewPose( viewDef, eye );

		// setup render matrices
		R_SetViewMatrix( *viewDef );
		idRenderMatrix::Transpose( *(idRenderMatrix*)viewDef->projectionMatrix, viewDef->projectionRenderMatrix );
		idRenderMatrix viewRenderMatrix;
		idRenderMatrix::Transpose( *(idRenderMatrix*)viewDef->worldSpace.modelViewMatrix, viewRenderMatrix );
		idRenderMatrix::Multiply( viewDef->projectionRenderMatrix, viewRenderMatrix, viewDef->worldSpace.mvp );

		memcpy( viewDef->eyeToHead.ToFloatPtr(), viewDef->worldSpace.modelViewMatrix, sizeof(idMat4) );
		viewDef->eyeToHead.InverseSelf();
		viewDef->eyeToHead = viewDef->eyeToHead * viewDef->headView;

		// apply new view matrix to view and all entities
		for ( viewEntity_t *vEntity = viewDef->viewEntitys; vEntity; vEntity = vEntity->next ) {
			myGlMultMatrix( vEntity->modelMatrix, viewDef->worldSpace.modelViewMatrix, vEntity->modelViewMatrix );
		}

		// GUI surfs are not included in the vEntity list, so need to be modified separately
		for ( int i = 0; i < viewDef->numDrawSurfs; ++i ) {
			drawSurf_t *surf = viewDef->drawSurfs[i];
			if ( surf->dsFlags & DSF_GUI_SURF ) {
				myGlMultMatrix( surf->space->modelMatrix, viewDef->worldSpace.modelViewMatrix, const_cast<viewEntity_t*>(surf->space)->modelViewMatrix );
			}
		}

		if ( viewDef->renderView.viewID == VID_PLAYER_VIEW ) {
			idVec3 aimViewPos;
			R_LocalPointToGlobal( viewDef->worldSpace.modelViewMatrix, aimIndicatorPos, aimViewPos );
			idMat4 modelView (mat3_identity, aimViewPos);
			modelView.TransposeSelf();
			myGlMultMatrix( modelView.ToFloatPtr(), viewDef->projectionMatrix, aimIndicatorMvp.ToFloatPtr() );
		}

		if ( vr_useHiddenAreaMesh.GetBool() ) {
			idScreenRect visibleAreaScissor;
			visibleAreaScissor.x1 = visibleAreaBounds[eye][0] * glConfig.vidWidth;
			visibleAreaScissor.y1 = visibleAreaBounds[eye][1] * glConfig.vidHeight;
			visibleAreaScissor.x2 = visibleAreaBounds[eye][2] * glConfig.vidWidth;
			visibleAreaScissor.y2 = visibleAreaBounds[eye][3] * glConfig.vidHeight;
			viewDef->scissor.Intersect( visibleAreaScissor );
		}
	}
}

void OpenXRBackend::SetupProjectionMatrix( viewDef_t *viewDef, int eye ) {
	const float zNear = (viewDef->renderView.cramZNear) ? (r_znear.GetFloat() * 0.25f) : r_znear.GetFloat();
	float angleLeft, angleRight, angleUp, angleDown;
	GetFov( eye, angleLeft, angleRight, angleUp, angleDown );
	const float idx = 1.0f / (tan(angleRight) - tan(angleLeft));
	const float idy = 1.0f / (tan(angleUp) - tan(angleDown));
	const float sx = tan(angleRight) + tan(angleLeft);
	const float sy = tan(angleDown) + tan(angleUp);

	viewDef->projectionMatrix[0 * 4 + 0] = 2.0f * idx;
	viewDef->projectionMatrix[1 * 4 + 0] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 0] = sx * idx;
	viewDef->projectionMatrix[3 * 4 + 0] = 0.0f;

	viewDef->projectionMatrix[0 * 4 + 1] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 1] = 2.0f * idy;
	viewDef->projectionMatrix[2 * 4 + 1] = sy*idy;	
	viewDef->projectionMatrix[3 * 4 + 1] = 0.0f;

	viewDef->projectionMatrix[0 * 4 + 2] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 2] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 2] = -0.999f; 
	viewDef->projectionMatrix[3 * 4 + 2] = -2.0f * zNear;

	viewDef->projectionMatrix[0 * 4 + 3] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 3] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 3] = -1.0f;
	viewDef->projectionMatrix[3 * 4 + 3] = 0.0f;
}

extern void R_MirrorPoint( const idVec3 in, orientation_t *surface, orientation_t *camera, idVec3 &out );
extern void R_MirrorVector( const idVec3 in, orientation_t *surface, orientation_t *camera, idVec3 &out );

void OpenXRBackend::UpdateViewPose( viewDef_t *viewDef, int eye ) {
	idVec3 position;
	idQuat orientation;
	if ( !GetCurrentEyePose( eye, position, orientation ) ) {
		return;
	}
	idMat3 axis = orientation.ToMat3();

	// update with new pose
	if ( viewDef->isMirror ) {
		assert( viewDef->superView != nullptr );
		renderView_t *origRv = nullptr;
		// find the nearest mirror info stored in this or parent views
		for ( viewDef_t *vd = viewDef; vd; vd = vd->superView ) {
			if ( vd->renderView.isMirror ) {
				origRv = &vd->renderView;
				break;
			}
		}

		if ( origRv == nullptr ) {
			common->Warning( "Failed to find mirror info, not adjusting view..." );
			return;
		}

		if ( origRv == &viewDef->renderView ) {
			// update mirror axis and origin
			idMat3 origaxis = axis * origRv->initialViewaxis;
			idVec3 origpos = origRv->initialVieworg + position * origRv->initialViewaxis;
			// set the mirrored origin and axis
			R_MirrorPoint( origpos, &origRv->mirrorSurface, &origRv->mirrorCamera, viewDef->renderView.vieworg );

			R_MirrorVector( origaxis[0], &origRv->mirrorSurface, &origRv->mirrorCamera, viewDef->renderView.viewaxis[0] );
			R_MirrorVector( origaxis[1], &origRv->mirrorSurface, &origRv->mirrorCamera, viewDef->renderView.viewaxis[1] );
			R_MirrorVector( origaxis[2], &origRv->mirrorSurface, &origRv->mirrorCamera, viewDef->renderView.viewaxis[2] );
		} else {
			// recalculate mirrored axis from parent mirror view
			idMat3 mirroredAxis = origRv->viewaxis * origRv->initialViewaxis.Inverse() * viewDef->renderView.initialViewaxis;
			if ( !viewDef->renderView.fixedOrigin ) {
				idVec3 mirroredPos = viewDef->renderView.initialVieworg + position * viewDef->renderView.initialViewaxis;
			}
		}
	} else {
		renderView_t& eyeView = viewDef->renderView;
		eyeView.viewaxis = axis * eyeView.initialViewaxis;
		if ( !eyeView.fixedOrigin ) {
			eyeView.vieworg = eyeView.initialVieworg + position * eyeView.initialViewaxis;
		}
	}

	if ( viewDef->isSubview ) {
		// can't use the calculated view scissors, and no simple way to recalculate them...
		viewDef->scissor = viewDef->superView->scissor;
	}
}

struct MirrorUniforms : GLSLUniformGroup {
	UNIFORM_GROUP_DEF( MirrorUniforms )
	DEFINE_UNIFORM( float, aspectEye )
	DEFINE_UNIFORM( float, aspectMirror )
	DEFINE_UNIFORM( sampler, vrEye )
	DEFINE_UNIFORM( sampler, ui)
};

void OpenXRBackend::MirrorVrView( idImage *eyeTexture, idImage *uiTexture ) {
	GL_PROFILE( "VrMirrorView" )

	GL_ViewportRelative( 0, 0, 1, 1 );
	GL_ScissorRelative( 0, 0, 1, 1 );

	vrMirrorShader->Activate();
	MirrorUniforms *uniforms = vrMirrorShader->GetUniformGroup<MirrorUniforms>();
	uniforms->aspectEye.Set( static_cast<float>(eyeTexture->uploadWidth) / eyeTexture->uploadHeight );
	uniforms->aspectMirror.Set( static_cast<float>(glConfig.windowWidth) / glConfig.windowHeight );
	uniforms->vrEye.Set( 0 );
	uniforms->ui.Set( 1 );

	GL_SelectTexture( 1 );
	uiTexture->Bind();
	GL_SelectTexture( 0 );
	eyeTexture->Bind();

	qglClearColor( 0, 0, 0, 1 );
	qglClear(GL_COLOR_BUFFER_BIT);
	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

	if ( UsesSrgbTextures() ) {
		qglEnable( GL_FRAMEBUFFER_SRGB );
	}
	RB_DrawFullScreenQuad();
	qglDisable( GL_FRAMEBUFFER_SRGB );
}

struct AimIndicatorUniforms : GLSLUniformGroup {
	UNIFORM_GROUP_DEF( AimIndicatorUniforms )
	DEFINE_UNIFORM( mat4, mvp )
	DEFINE_UNIFORM( float, size )
	DEFINE_UNIFORM( vec4, color )
};

void OpenXRBackend::DrawAimIndicator( float size ) {
	if ( !vr_aimIndicator.GetBool() ) {
		return;
	}

	vrAimIndicatorShader->Activate();
	AimIndicatorUniforms *uniforms = vrAimIndicatorShader->GetUniformGroup<AimIndicatorUniforms>();
	uniforms->mvp.Set( aimIndicatorMvp );
	uniforms->size.Set( size );
	uniforms->color.Set( vr_aimIndicatorColorR.GetFloat(), vr_aimIndicatorColorG.GetFloat(), vr_aimIndicatorColorB.GetFloat(), vr_aimIndicatorColorA.GetFloat() );
	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHMASK | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_SRCBLEND_SRC_ALPHA );
	RB_DrawFullScreenQuad();	
}

void OpenXRBackend::UpdateFrameStatus( const frameData_t *frameData ) {
	float positionDifference = (lastCameraPosition - frameData->cameraPosition).LengthSqr();
	if ( ! frameData->cameraAngles.Compare(lastCameraAngles, 0.0001f) || positionDifference > 0.0001f ) {
		vignetteEnabled = true;
		lastCameraUpdateTime = Sys_GetTimeMicroseconds();
	} else {
		if ( Sys_GetTimeMicroseconds() - lastCameraUpdateTime >= 500000 ) {
			// disable vignette after a 0.5s delay
			vignetteEnabled = false;
		}
	}

	lastCameraAngles = frameData->cameraAngles;
	lastCameraPosition = frameData->cameraPosition;
}

struct ComfortVignetteUniforms : GLSLUniformGroup {
	UNIFORM_GROUP_DEF( ComfortVignetteUniforms )
	DEFINE_UNIFORM( float, radius )
	DEFINE_UNIFORM( float, corridor )
	DEFINE_UNIFORM( vec2, center)
};

idVec2 OpenXRBackend::ProjectCenterUV( int eye ) {
	// since we are dealing with an asymmetrical projection, we need to figure out the projection center
	// see: https://developer.oculus.com/blog/tech-note-asymmetric-field-of-view-faq/
	viewDef_t stubView;
	memset( &stubView, 0, sizeof(stubView) );
	SetupProjectionMatrix( &stubView, eye );

	idVec4 projectedCenter;
	R_PointTimesMatrix( stubView.projectionMatrix, idVec4(0, 0, -r_znear.GetFloat(), 1), projectedCenter );
	projectedCenter.x /= projectedCenter.w;
	projectedCenter.y /= projectedCenter.w;
	return idVec2( 0.5f * (projectedCenter.x + 1), 0.5f * (projectedCenter.y + 1) );
}

void OpenXRBackend::DrawComfortVignette(eyeView_t eye) {
	// draw a circular cutout
	idVec2 uv = ProjectCenterUV( eye );
	
	comfortVignetteShader->Activate();
	ComfortVignetteUniforms *uniforms = comfortVignetteShader->GetUniformGroup<ComfortVignetteUniforms>();
	uniforms->radius.Set( vr_comfortVignetteRadius.GetFloat() );
	uniforms->corridor.Set( vr_comfortVignetteCorridor.GetFloat() );
	uniforms->center.Set( uv );
	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	RB_DrawFullScreenQuad();
}

void OpenXRBackend::UpdateLightScissor( viewLight_t *vLight ) {
	if ( vr_useLightScissors.GetBool() ) {
		vLight->scissorRect = VR_CalcLightScissorRectangle( vLight, backEnd.viewDef );
	} else {
		vLight->scissorRect = backEnd.viewDef->scissor;
		vLight->scissorRect.zmin = 0;
		vLight->scissorRect.zmax = 1;
	}
}

bool OpenXRBackend::UseRadialDensityMask() {
	return vr_useFixedFoveatedRendering.GetInteger() == 2
		|| (vr_useFixedFoveatedRendering.GetInteger() == 1 && !GLAD_GL_NV_shading_rate_image);
}

#ifdef WIN32
#include "OpenXRSwapchainDX.h"
#include "../../sys/win32/win_local.h"
typedef XrGraphicsBindingOpenGLWin32KHR XrGraphicsBindingGL;
XrGraphicsBindingOpenGLWin32KHR Sys_CreateGraphicsBindingGL() {
	return {
		XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR,
		nullptr,
		win32.hDC,
		win32.hGLRC,
	};
}

#include "D3D11Helper.h"
namespace {
	D3D11Helper d3d11Helper;
}
#endif

#ifdef __linux__
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_X11 1
#define GLFW_EXPOSE_NATIVE_GLX 1
#include "GLFW/glfw3native.h"
extern GLFWwindow *window;

typedef XrGraphicsBindingOpenGLXlibKHR XrGraphicsBindingGL;
XrGraphicsBindingOpenGLXlibKHR Sys_CreateGraphicsBindingGL() {
	Display *dpy = glfwGetX11Display();
	Window win = glfwGetX11Window( window );
	GLXContext ctx = glfwGetGLXContext( window );
	auto glXQueryContext = (PFNGLXQUERYCONTEXTPROC)glfwGetProcAddress( "glXQueryContext" );
	auto glXChooseFBConfig = (PFNGLXCHOOSEFBCONFIGPROC)glfwGetProcAddress( "glXChooseFBConfig" );
	auto glXGetVisualFromFBConfig = (PFNGLXGETVISUALFROMFBCONFIGPROC)glfwGetProcAddress( "glXGetVisualFromFBConfig" );
	int fbConfigId = 0;
	glXQueryContext( dpy, ctx, GLX_FBCONFIG_ID, &fbConfigId );
	common->Printf("FBConfig ID: %d\n", fbConfigId);
	int numConfigs = 0;
	int attribs[] = { GLX_FBCONFIG_ID, fbConfigId, None };
	GLXFBConfig *fbConfigs = glXChooseFBConfig( dpy, 0, attribs, &numConfigs );
	if ( numConfigs < 1 ) {
		common->FatalError( "Could not retrieve GLXFBConfig for OpenXR initialization" );
	}
	GLXFBConfig fbConfig = fbConfigs[0];
	XFree(fbConfigs);
	XVisualInfo *visinfo = glXGetVisualFromFBConfig( dpy, fbConfig );
	uint32_t visualId = XVisualIDFromVisual(visinfo->visual);

	return {
		XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
		nullptr,
		dpy,
		visualId,
		fbConfig,
		win,
		ctx,
	};
}
#endif

namespace {
	XrBool32 XRAPI_PTR XR_DebugMessengerCallback(
			XrDebugUtilsMessageSeverityFlagsEXT              messageSeverity,
			XrDebugUtilsMessageTypeFlagsEXT                  messageTypes,
			const XrDebugUtilsMessengerCallbackDataEXT*      callbackData,
			void*                                            userData) {
		common->Printf("XR in %s: %s\n", callbackData->functionName, callbackData->message);
		return XR_TRUE;
	}
}

void OpenXRBackend::UpdateInput( int axis[6], idList<padActionChange_t> &actionChanges, poseInput_t &poseInput ) {
	input.UpdateInput( axis, actionChanges, poseInput, seatedSpace, predictedFrameDisplayTime );
}

void OpenXRBackend::InitBackend() {
	if ( instance != nullptr ) {
		Destroy();
	}

	common->Printf( "-----------------------------\n" );
	common->Printf( "Initializing VR subsystem\n" );

	instance = XR_CreateInstance();

	if ( XR_EXT_debug_utils_available ) {
		SetupDebugMessenger();
	}
	
	XrSystemGetInfo systemGetInfo = {
		XR_TYPE_SYSTEM_GET_INFO,
		nullptr,
		XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
	};
	XrResult result = xrGetSystem( instance, &systemGetInfo, &system );
	XR_CheckResult( result, "acquiring the system id", instance );

	XrGraphicsBindingGL glGraphicsBinding;
	const void *graphicsBinding;
#ifdef WIN32
	XrGraphicsBindingD3D11KHR dxGraphicsBinding;
	if ( xr_preferD3D11.GetBool() && XR_KHR_D3D11_enable_available ) {
		usingD3D11 = true;
		XrGraphicsRequirementsD3D11KHR d3d11Reqs = {
			XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR,
			nullptr,
		};
		result = xrGetD3D11GraphicsRequirementsKHR( instance, system, &d3d11Reqs );
		XR_CheckResult( result, "calling D3D11 graphics requirements", instance );
		d3d11Helper.Init( d3d11Reqs );
		dxGraphicsBinding = d3d11Helper.CreateGraphicsBinding();
		graphicsBinding = &dxGraphicsBinding;

		uiSwapchain = new OpenXRSwapchainDX( &d3d11Helper );
		eyeSwapchains[0] = new OpenXRSwapchainDX( &d3d11Helper );
		eyeSwapchains[1] = new OpenXRSwapchainDX( &d3d11Helper );
	} else
#endif
	{
		usingD3D11 = true;
		// must call graphicsRequirements before opening a session, although the results are not
		// that interesting for us
		XrGraphicsRequirementsOpenGLKHR openglReqs = {
			XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR,
			nullptr,
		};
		result = xrGetOpenGLGraphicsRequirementsKHR( instance, system, &openglReqs );
		XR_CheckResult( result, "calling OpenGL graphics requirements", instance );

		glGraphicsBinding = Sys_CreateGraphicsBindingGL();
		graphicsBinding = &glGraphicsBinding;

		uiSwapchain = new OpenXRSwapchainGL;
		eyeSwapchains[0] = new OpenXRSwapchainGL;
		eyeSwapchains[1] = new OpenXRSwapchainGL;
	}
	XrSessionCreateInfo sessionCreateInfo = {
		XR_TYPE_SESSION_CREATE_INFO,
		graphicsBinding,
		0,
		system,
	};
	result = xrCreateSession( instance, &sessionCreateInfo, &session );
	XR_CheckResult( result, "creating session", instance );
	common->Printf( "Session created\n" );

	InitSwapchains();

	XrReferenceSpaceCreateInfo spaceCreateInfo = {
		XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		nullptr,
		XR_REFERENCE_SPACE_TYPE_LOCAL,
		{ {0, 0, 0, 1}, {0, 0, 0} },
	};
	result = xrCreateReferenceSpace( session, &spaceCreateInfo, &seatedSpace );
	XR_CheckResult( result, "creating seated reference space", instance );
	common->Printf( "Acquired seated reference space\n" );

	input.Init( instance, session );
	common->Printf( "Initialized XR input\n" );

	common->Printf( "-----------------------------\n" );

#ifdef __linux__
	// hack: current SteamVR OpenXR implementation is broken and does not properly reset the GL context after certain calls
	GLimp_ActivateContext();
#endif
}

void OpenXRBackend::DestroyBackend() {
	if ( instance == nullptr ) {
		return;
	}

	input.Destroy();
	uiSwapchain->Destroy();
	eyeSwapchains[0]->Destroy();
	eyeSwapchains[1]->Destroy();
	delete uiSwapchain;
	delete eyeSwapchains[0];
	delete eyeSwapchains[1];

	xrDestroySession( session );
	session = nullptr;

	if ( debugMessenger != nullptr ) {
		xrDestroyDebugUtilsMessengerEXT( debugMessenger );
	}

	xrDestroyInstance( instance );
	instance = nullptr;

#ifdef WIN32
	if ( usingD3D11 ) {
		d3d11Helper.Shutdown();
	}
#endif
}

bool OpenXRBackend::BeginFrame() {
	if ( !vrSessionActive ) {
		return false;
	}

	GL_PROFILE("XrBeginFrame")
	// await the next frame
	XrFrameState frameState = {
		XR_TYPE_FRAME_STATE,
		nullptr,
	};
	XrResult result = xrWaitFrame( session, nullptr, &frameState );
	XR_CheckResult( result, "awaiting frame", instance, false );
	if ( !XR_SUCCEEDED( result ) ) {
		return false;
	}

	predictedFrameDisplayTime = frameState.predictedDisplayTime;
	displayPeriod = frameState.predictedDisplayPeriod;
	if ( !frameState.shouldRender ) {
		common->Printf( "Should not render current frame...\n" );
	}

	result = xrBeginFrame( session, nullptr );
	XR_CheckResult( result, "beginning frame", instance, false );
	if ( !XR_SUCCEEDED( result ) ) {
		return false;
	}

	// predict render poses
	XrViewLocateInfo locateInfo = {
		XR_TYPE_VIEW_LOCATE_INFO,
		nullptr,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		predictedFrameDisplayTime,
		seatedSpace,
	};
	XrViewState viewState = {
		XR_TYPE_VIEW_STATE,
		nullptr,
	};
	for ( int i = 0; i < 2; ++i ) {
		renderViews[i].type = XR_TYPE_VIEW;
		renderViews[i].next = nullptr;
	}
	uint32_t viewCount = 0;
	result = xrLocateViews( session, &locateInfo, &viewState, 2, &viewCount, renderViews );
	XR_CheckResult( result, "locating views", instance, false );

	return true;
}

void OpenXRBackend::PrepareFrame() {
	GL_PROFILE("XrPrepareFrame")

	// poll xr events and react to them
	XrEventDataBuffer event = {
		XR_TYPE_EVENT_DATA_BUFFER,
		nullptr,
	};
	XrResult result = xrPollEvent( instance, &event );
	XR_CheckResult( result, "polling events", instance );
	if ( result == XR_SUCCESS ) {
		common->Printf( "Received VR event: %d\n", event.type );
		switch ( event.type ) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			HandleSessionStateChange( *reinterpret_cast<XrEventDataSessionStateChanged*>(&event) );
			break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			common->FatalError( "xr Instance lost" );
		}
	}

	if ( !vrSessionActive ) {
		return;
	}

	// predict poses for the frontend to work with
	// note: the predicted display time was for the frame last rendered by the backend,
	// so we need to predict two frames into the future from there: 1 for the current backend frame,
	// and 1 for the next frame the frontend is preparing
	XrViewLocateInfo locateInfo = {
		XR_TYPE_VIEW_LOCATE_INFO,
		nullptr,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		predictedFrameDisplayTime + 2 * displayPeriod,
		seatedSpace,
	};
	XrViewState viewState = {
		XR_TYPE_VIEW_STATE,
		nullptr,
	};
	for ( int i = 0; i < 2; ++i ) {
		predictedViews[i].type = XR_TYPE_VIEW;
		predictedViews[i].next = nullptr;
	}
	uint32_t viewCount = 0;
	result = xrLocateViews( session, &locateInfo, &viewState, 2, &viewCount, predictedViews );
	XR_CheckResult( result, "locating views", instance, false );
}

void OpenXRBackend::SubmitFrame() {
	if ( !vrSessionActive ) {
		return;
	}

	GL_PROFILE("XrSubmitFrame")

#ifdef __linux__
	// hack: the SteamVR OpenXR runtime does not properly set viewport and scissor before copying render textures :(
	qglViewport(0, 0, 10000, 10000);
	qglScissor(0, 0, 10000, 10000);
#endif

	uiSwapchain->ReleaseImage();
	eyeSwapchains[0]->ReleaseImage();
	eyeSwapchains[1]->ReleaseImage();

	XrCompositionLayerProjectionView projectionViews[2] = {
		{
			XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			nullptr,
			renderViews[0].pose,
			renderViews[0].fov,
			eyeSwapchains[0]->CurrentSwapchainSubImage(),
		},
		{
			XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			nullptr,
			renderViews[1].pose,
			renderViews[1].fov,
			eyeSwapchains[1]->CurrentSwapchainSubImage(),
		},
	};
	XrCompositionLayerProjection stereoLayer = {
		XR_TYPE_COMPOSITION_LAYER_PROJECTION,
		nullptr,
		0,
		seatedSpace,
		2,
		projectionViews,
	};
	XrCompositionLayerQuad uiLayer = {
		XR_TYPE_COMPOSITION_LAYER_QUAD,
		nullptr,
		XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
		seatedSpace,
		XR_EYE_VISIBILITY_BOTH,
		uiSwapchain->CurrentSwapchainSubImage(),
		{ {0, 0, 0, 1}, {0, vr_uiOverlayVerticalOffset.GetFloat(), -vr_uiOverlayDistance.GetFloat()} },
		{ vr_uiOverlayHeight.GetFloat() * vr_uiOverlayAspect.GetFloat(), vr_uiOverlayHeight.GetFloat() },
	};
	idList<const XrCompositionLayerBaseHeader *> submittedLayers = {
		reinterpret_cast< const XrCompositionLayerBaseHeader* >( &stereoLayer ),
		reinterpret_cast< const XrCompositionLayerBaseHeader* >( &uiLayer ),
	};
	XrFrameEndInfo frameEndInfo = {
		XR_TYPE_FRAME_END_INFO,
		nullptr,
		predictedFrameDisplayTime,
		XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		submittedLayers.Num(),
		submittedLayers.Ptr(),
	};
	XrResult result = xrEndFrame( session, &frameEndInfo );
	XR_CheckResult( result, "submitting frame", instance, false );

#ifdef __linux__
	// hack: current SteamVR OpenXR implementation is broken and does not properly reset the GL context after certain calls
	GLimp_ActivateContext();
#endif
}

void OpenXRBackend::GetFov( int eye, float &angleLeft, float &angleRight, float &angleUp, float &angleDown ) {
	const XrFovf &fov = renderViews[eye].fov;
	angleLeft = fov.angleLeft;
	angleRight = fov.angleRight;
	angleUp = fov.angleUp;
	angleDown = fov.angleDown;
}

bool OpenXRBackend::GetCurrentEyePose( int eye, idVec3 &origin, idQuat &orientation ) {
	if ( !vrSessionActive ) {
		return false;
	}

	const XrPosef &pose = renderViews[eye].pose;
	origin = Vec3FromXr( pose.position );
	orientation = QuatFromXr( pose.orientation );

	return true;
}

bool OpenXRBackend::GetPredictedEyePose( int eye, idVec3 &origin, idQuat &orientation ) {
	if ( !vrSessionActive ) {
		return false;
	}

	const XrPosef &pose = predictedViews[eye].pose;
	origin = Vec3FromXr( pose.position );
	orientation = QuatFromXr( pose.orientation );

	return true;
}

void OpenXRBackend::AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture ) {
	if ( eye == UI ) {
		uiSwapchain->PrepareNextImage();
		fbo = uiSwapchain->CurrentFrameBuffer();
		texture = uiSwapchain->CurrentImage();
	} else {
		eyeSwapchains[eye]->PrepareNextImage();
		fbo = eyeSwapchains[eye]->CurrentFrameBuffer();
		texture = eyeSwapchains[eye]->CurrentImage();
	}
}

idList<idVec2> OpenXRBackend::GetHiddenAreaMask( eyeView_t eye ) {
	if ( !XR_KHR_visibility_mask_available ) {
		return idList<idVec2>();
	}
	
	XrVisibilityMaskKHR visibilityMask = {
		XR_TYPE_VISIBILITY_MASK_KHR,
		nullptr,
		0,
		0,
		nullptr,
		0,
		0,
		nullptr,
	};
	XrResult result = xrGetVisibilityMaskKHR( session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye,
		XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &visibilityMask );
	XR_CheckResult( result, "getting hidden area mesh", instance, false );
	if ( !XR_SUCCEEDED(result) ) {
		return idList<idVec2>();
	}

	idList<XrVector2f> vertices;
	vertices.SetNum( visibilityMask.vertexCountOutput );
	visibilityMask.vertexCapacityInput = vertices.Num();
	visibilityMask.vertices = vertices.Ptr();
	idList<uint32_t> indices;
	indices.SetNum( visibilityMask.indexCountOutput );
	visibilityMask.indexCapacityInput = indices.Num();
	visibilityMask.indices = indices.Ptr();

	result = xrGetVisibilityMaskKHR( session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye,
		XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, &visibilityMask );
	XR_CheckResult( result, "getting hidden area mesh", instance, false );
	if ( !XR_SUCCEEDED(result) ) {
		return idList<idVec2>();
	}

	idList<idVec2> hiddenAreaMask;
	for ( uint32_t index : indices ) {
		idVec2 v ( vertices[index].x * 2 - 1, vertices[index].y * 2 - 1 );
		hiddenAreaMask.AddGrow( v );
	}

	return hiddenAreaMask;
}

idVec4 OpenXRBackend::GetVisibleAreaBounds( eyeView_t eye ) {
	if ( !XR_KHR_visibility_mask_available ) {
		return idVec4( 0, 0, 1, 1 );
	}
	
	XrVisibilityMaskKHR visibilityMask = {
		XR_TYPE_VISIBILITY_MASK_KHR,
		nullptr,
		0,
		0,
		nullptr,
		0,
		0,
		nullptr,
	};
	XrResult result = xrGetVisibilityMaskKHR( session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye,
		XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR, &visibilityMask );
	XR_CheckResult( result, "getting visible area mesh", instance, false );
	if ( !XR_SUCCEEDED(result) ) {
		return idVec4( 0, 0, 1, 1 );
	}

	idList<XrVector2f> vertices;
	vertices.SetNum( visibilityMask.vertexCountOutput );
	visibilityMask.vertexCapacityInput = vertices.Num();
	visibilityMask.vertices = vertices.Ptr();
	idList<uint32_t> indices;
	indices.SetNum( visibilityMask.indexCountOutput );
	visibilityMask.indexCapacityInput = indices.Num();
	visibilityMask.indices = indices.Ptr();

	result = xrGetVisibilityMaskKHR( session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eye,
		XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR, &visibilityMask );
	XR_CheckResult( result, "getting visible area mesh", instance, false );
	if ( !XR_SUCCEEDED(result) ) {
		return idVec4( 0, 0, 1, 1 );
	}

	idVec4 bounds (1, 1, 0, 0);
	for ( uint32_t index : indices ) {
		bounds[0] = Min( bounds[0], vertices[index].x );
		bounds[1] = Min( bounds[1], vertices[index].y );
		bounds[2] = Max( bounds[2], vertices[index].x );
		bounds[3] = Max( bounds[3], vertices[index].y );
	}

	return bounds;
	
}

bool OpenXRBackend::UsesSrgbTextures() const {
#ifdef WIN32
	return usingD3D11 ? ( swapchainFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) : ( swapchainFormat == GL_SRGB8_ALPHA8 );
#else
	return swapchainFormat == GL_SRGB8_ALPHA8;
#endif
}

void OpenXRBackend::SetupDebugMessenger() {
	XrDebugUtilsMessengerCreateInfoEXT createInfo = {
		XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		nullptr,
		0xffffffff,
		0xffffffff,
		&XR_DebugMessengerCallback,
		nullptr,
	};
	XrResult result = xrCreateDebugUtilsMessengerEXT( instance, &createInfo, &debugMessenger );
	XR_CheckResult( result, "setting up debug messenger", instance );
	common->Printf( "Enabled debug messenger\n" );
}

void OpenXRBackend::ChooseSwapchainFormat() {
	uint32_t numSupportedFormats = 0;
	XrResult result = xrEnumerateSwapchainFormats( session, 0, &numSupportedFormats, nullptr );
	XR_CheckResult( result, "getting supported swapchain formats", instance );
	idList<int64_t> swapchainFormats;
	swapchainFormats.SetNum( numSupportedFormats );
	result = xrEnumerateSwapchainFormats( session, swapchainFormats.Num(), &numSupportedFormats, swapchainFormats.Ptr() );
	XR_CheckResult( result, "getting supported swapchain formats", instance );

	// find first matching desired swapchain format
	swapchainFormat = 0;
	std::set<int64_t> supportedFormats (swapchainFormats.begin(), swapchainFormats.end());
	idList<int64_t> desiredFormats;
	idList<idStr> formatNames;
#ifdef WIN32
	if ( usingD3D11 ) {
		desiredFormats = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT };
		formatNames = { "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB", "DXGI_FORMAT_R8G8B8A8_UNORM", "DXGI_FORMAT_R16G16B16A16_FLOAT" };
	} else
#endif
	{
		 desiredFormats = { GL_SRGB8_ALPHA8, GL_RGBA8, GL_RGBA16F };
		 formatNames = { "GL_SRGB8_ALPHA8", "GL_RGBA8", "GL_RGBA16F" };
	}
	for ( int i = 0; i < desiredFormats.Num(); ++i ) {
		if ( supportedFormats.find( desiredFormats[i] ) != supportedFormats.end() ) {
			swapchainFormat = desiredFormats[i];
			common->Printf( "Using swapchain format: %s\n", formatNames[i].c_str() );
			break;
		}
	}
	if ( swapchainFormat == 0 ) {
		common->FatalError( "No suitable supported swapchain format found" );
	}
}

void OpenXRBackend::InitSwapchains() {
	ChooseSwapchainFormat();

	uint32_t numViews = 0;
	for ( int i = 0; i < 2; ++i ) {
		views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		views[i].next = nullptr;
	}
	XrResult result = xrEnumerateViewConfigurationViews( instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		2, &numViews, views );
	XR_CheckResult( result, "getting view info", instance );
	common->Printf( "Recommended render resolution: %dx%d\n", views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight );

	// hack: force primary fbo to use our desired render resolution
	glConfig.vidWidth = views[0].recommendedImageRectWidth;
	glConfig.vidHeight = views[0].recommendedImageRectHeight;
	r_fboResolution.SetModified();
	
	eyeSwapchains[0]->Init( "leftEye", swapchainFormat, views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight );
	eyeSwapchains[1]->Init( "rightEye", swapchainFormat, views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight );
	uiSwapchain->Init( "ui", swapchainFormat, vr_uiResolution.GetInteger(), vr_uiResolution.GetInteger() );

	common->Printf( "Created swapchains\n" );
}

void OpenXRBackend::HandleSessionStateChange( const XrEventDataSessionStateChanged &stateChangedEvent ) {
	XrSessionBeginInfo beginInfo = {
		XR_TYPE_SESSION_BEGIN_INFO,
		nullptr,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	};
	XrResult result;

	switch ( stateChangedEvent.state ) {
	case XR_SESSION_STATE_READY:
		common->Printf( "xr Session is ready, beginning session...\n" );
		result = xrBeginSession( session, &beginInfo );
		XR_CheckResult( result, "beginning session", instance );
		vrSessionActive = true;
		break;

	case XR_SESSION_STATE_SYNCHRONIZED:
	case XR_SESSION_STATE_VISIBLE:
	case XR_SESSION_STATE_FOCUSED:
		common->Printf( "xr Session restored" );
		vrSessionActive = true;
		break;

	case XR_SESSION_STATE_IDLE:
		vrSessionActive = false;
		break;

	case XR_SESSION_STATE_STOPPING:
		vrSessionActive = false;
		common->Printf( "xr Session lost or stopped" );
		result = xrEndSession( session );
		XR_CheckResult( result, "ending session", instance, false );
		break;
	case XR_SESSION_STATE_LOSS_PENDING:
		common->FatalError( "xr Session lost" );
	}	
}
