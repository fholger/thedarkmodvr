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
#include "VRBackend.h"
#include "OpenXRBackend.h"
#include "OpenVRBackend.h"
#include "../FrameBuffer.h"
#include "../FrameBufferManager.h"
#include "../GLSLProgram.h"
#include "../GLSLProgramManager.h"
#include "../GLSLUniforms.h"
#include "../Profiling.h"
#include "../backend/RenderBackend.h"

VRBackend *vrBackend = nullptr;

idCVar vr_backend( "vr_backend", "0", CVAR_RENDERER|CVAR_INTEGER|CVAR_ARCHIVE, "0 - OpenVR, 1 - OpenXR" );
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

extern void RB_Tonemap( bloomCommand_t *cmd );
extern void RB_CopyRender( const void *data );

const float VRBackend::GameUnitsToMetres = 0.02309f;
const float VRBackend::MetresToGameUnits = 1.0f / GameUnitsToMetres;

void VRBackend::Init() {
	InitBackend();
	InitHiddenAreaMesh();

	vrMirrorShader = programManager->LoadFromFiles( "vr_mirror", "vr/vr_mirror.vert.glsl", "vr/vr_mirror.frag.glsl" );
	vrAimIndicatorShader = programManager->LoadFromFiles( "aim_indicator", "vr/aim_indicator.vert.glsl", "vr/aim_indicator.frag.glsl" );
	comfortVignetteShader = programManager->LoadFromFiles( "comfort_vignette", "vr/comfort_vignette.vert.glsl", "vr/comfort_vignette.frag.glsl" );
}

void VRBackend::Destroy() {
	vrAimIndicatorShader = nullptr;
	vrMirrorShader = nullptr;
	qglDeleteBuffers( 1, &hiddenAreaMeshBuffer );
	hiddenAreaMeshBuffer = 0;
	hiddenAreaMeshShader = nullptr;
	DestroyBackend();
}

void VRBackend::AdjustRenderView( renderView_t *view ) {
	idVec3 leftEyePos, rightEyePos;
	idQuat leftEyeRot, rightEyeRot;
	if ( !GetPredictedEyePose( LEFT_EYE, leftEyePos, leftEyeRot ) || !GetPredictedEyePose( RIGHT_EYE, rightEyePos, rightEyeRot ) ) {
		return;
	}

	float fov[2][4];
	GetFov( LEFT_EYE, fov[0][0], fov[0][1], fov[0][2], fov[0][3] );
	GetFov( RIGHT_EYE, fov[1][0], fov[1][1], fov[1][2], fov[1][3] );
	// Some headsets have canted lenses. We determine the angle between the forward vectors (x axes) of the eyes
	// and add that to the horizontal FOV to ensure we are not clipping visible objects
	idVec3 leftEyeForward = leftEyeRot.ToMat3()[0];
	idVec3 rightEyeForward = rightEyeRot.ToMat3()[0];
	float cantedAngle = idMath::Fabs( idMath::ACos( leftEyeForward * rightEyeForward ) );
	view->fov_x = RAD2DEG( cantedAngle + 2 * Max( idMath::Fabs(fov[0][0]), idMath::Fabs(fov[1][1]) ) );
	view->fov_y = RAD2DEG( 2 * Max( idMath::Fabs(fov[0][2]), idMath::Fabs(fov[0][3]) ) );
	// add a bit extra to the FOV since the view may have moved slightly when we actually render,
	// but ensure not to reach 180° or more, as the math breaks down at that point
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
}

void VRBackend::RenderStereoView( const frameData_t *frameData ) {
	if ( !BeginFrame() ) {
		return;
	}

	UpdateComfortVignetteStatus( frameData );

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

	aimIndicatorPos = frameData->mouseAimPosition;

	// render stereo views
	for ( int eye = 0; eye < 2; ++eye ) {
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

void VRBackend::DrawHiddenAreaMeshToDepth() {
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
	GL_Cull( CT_BACK_SIDED );
	vertexCache.UnbindVertex();
}

void VRBackend::ExecuteRenderCommands( const frameData_t *frameData, eyeView_t eyeView ) {
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
			RB_Tonemap( (bloomCommand_t*)cmds );
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
}

extern void R_SetupViewFrustum( viewDef_t *viewDef );

void VRBackend::InitHiddenAreaMesh() {
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

	hiddenAreaMeshShader = programManager->LoadFromFiles( "hidden_area_mesh", "vr/hidden_area_mesh.vert.glsl", "vr/hidden_area_mesh.frag.glsl" );
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

void VRBackend::UpdateRenderViewsForEye( const emptyCommand_t *cmds, int eye ) {
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

		SetupProjectionMatrix( viewDef, eye );
		UpdateViewPose( viewDef, eye );

		// setup render matrices
		R_SetViewMatrix( *viewDef );
		idRenderMatrix::Transpose( *(idRenderMatrix*)viewDef->projectionMatrix, viewDef->projectionRenderMatrix );
		idRenderMatrix viewRenderMatrix;
		idRenderMatrix::Transpose( *(idRenderMatrix*)viewDef->worldSpace.modelViewMatrix, viewRenderMatrix );
		idRenderMatrix::Multiply( viewDef->projectionRenderMatrix, viewRenderMatrix, viewDef->worldSpace.mvp );

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

void VRBackend::SetupProjectionMatrix( viewDef_t *viewDef, int eye ) {
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

void VRBackend::UpdateViewPose( viewDef_t *viewDef, int eye ) {
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

		renderView_t &parentRv = viewDef->superView->renderView;
		renderView_t &eyeView = viewDef->renderView;
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

void VRBackend::MirrorVrView( idImage *eyeTexture, idImage *uiTexture ) {
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

void VRBackend::DrawAimIndicator( float size ) {
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

void VRBackend::UpdateComfortVignetteStatus( const frameData_t *frameData ) {
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

void VRBackend::DrawComfortVignette(eyeView_t eye) {
	// draw a circular cutout
	// since we are dealing with an asymmetrical projection, we need to figure out the projection center
	// see: https://developer.oculus.com/blog/tech-note-asymmetric-field-of-view-faq/
	viewDef_t stubView;
	memset( &stubView, 0, sizeof(stubView) );
	SetupProjectionMatrix( &stubView, eye );

	idVec4 projectedVignetteCenter;
	R_PointTimesMatrix( stubView.projectionMatrix, idVec4(0, 0, -r_znear.GetFloat(), 1), projectedVignetteCenter );
	projectedVignetteCenter.x /= projectedVignetteCenter.w;
	projectedVignetteCenter.y /= projectedVignetteCenter.w;
	idVec2 uv( 0.5f * (projectedVignetteCenter.x + 1), 0.5f * (projectedVignetteCenter.y + 1) );
	
	comfortVignetteShader->Activate();
	ComfortVignetteUniforms *uniforms = comfortVignetteShader->GetUniformGroup<ComfortVignetteUniforms>();
	uniforms->radius.Set( vr_comfortVignetteRadius.GetFloat() );
	uniforms->corridor.Set( vr_comfortVignetteCorridor.GetFloat() );
	uniforms->center.Set( uv );
	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	RB_DrawFullScreenQuad();
}

void SelectVRImplementation() {
	if ( vr_backend.GetInteger() == 0 ) {
		vrBackend = openvr;
	} else {
		vrBackend = xrBackend;	
	}
}

void VRBackend::UpdateLightScissor( viewLight_t *vLight ) {
	if ( vr_useLightScissors.GetBool() ) {
		vLight->scissorRect = VR_CalcLightScissorRectangle( vLight, backEnd.viewDef );
	} else {
		vLight->scissorRect = backEnd.viewDef->scissor;
		vLight->scissorRect.zmin = 0;
		vLight->scissorRect.zmax = 1;
	}
}
