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
#include "xr_loader.h"
#include "xr_math.h"
#include "../tr_local.h"
#include "../FrameBuffer.h"
#include "../Profiling.h"

OpenXRBackend xrImpl;
OpenXRBackend *xrBackend = &xrImpl;

#ifdef WIN32
#include "../../sys/win32/win_local.h"
XrGraphicsBindingOpenGLWin32KHR Sys_CreateGraphicsBinding() {
	return {
		XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR,
		nullptr,
		win32.hDC,
		win32.hGLRC,
	};
}
#endif

#ifdef __linux__
#include "../../sys/linux/local.h"
XrGraphicsBindingOpenGLXlibKHR Sys_CreateGraphicsBinding() {
    uint32_t visualId = XVisualIDFromVisual(visinfo->visual);
    return {
        XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
        nullptr,
        dpy,
        visualId,
        bestFbc,
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

	// must call graphicsRequirements before opening a session, although the results are not
	// that interesting for us
	XrGraphicsRequirementsOpenGLKHR openglReqs = {
		XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR,
		nullptr,
	};
	result = xrGetOpenGLGraphicsRequirementsKHR( instance, system, &openglReqs );
	XR_CheckResult( result, "calling OpenGL graphics requirements", instance );

	auto graphicsBinding = Sys_CreateGraphicsBinding();
	XrSessionCreateInfo sessionCreateInfo = {
		XR_TYPE_SESSION_CREATE_INFO,
		&graphicsBinding,
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

	common->Printf( "-----------------------------\n" );
}

void OpenXRBackend::DestroyBackend() {
	if ( instance == nullptr ) {
		return;
	}

	uiSwapchain.Destroy();
	eyeSwapchains[0].Destroy();
	eyeSwapchains[1].Destroy();

	xrDestroySession( session );
	session = nullptr;

	if ( debugMessenger != nullptr ) {
		xrDestroyDebugUtilsMessengerEXT( debugMessenger );
	}

	xrDestroyInstance( instance );
	instance = nullptr;
}

void OpenXRBackend::AwaitFrame() {
	
}

void OpenXRBackend::GetFrontendPoses() {
	GL_PROFILE("XrBeginFrame")
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

	// await and begin the frame
	XrFrameState frameState = {
		XR_TYPE_FRAME_STATE,
		nullptr,
	};
	result = xrWaitFrame( session, nullptr, &frameState );
	XR_CheckResult( result, "awaiting frame", instance );
	currentFrameDisplayTime = frameState.predictedDisplayTime;
	nextFrameDisplayTime = currentFrameDisplayTime + frameState.predictedDisplayPeriod;

	XrViewLocateInfo locateInfo = {
		XR_TYPE_VIEW_LOCATE_INFO,
		nullptr,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		currentFrameDisplayTime,
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

	result = xrBeginFrame( session, nullptr );
	XR_CheckResult( result, "beginning frame", instance );
}

void OpenXRBackend::SubmitFrame() {
	if ( !vrSessionActive ) {
		return;
	}

	GL_PROFILE("XrSubmitFrame")

	uiSwapchain.ReleaseImage();
	eyeSwapchains[0].ReleaseImage();
	eyeSwapchains[1].ReleaseImage();

	XrCompositionLayerProjectionView projectionViews[2] = {
		{
			XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			nullptr,
			renderViews[0].pose,
			renderViews[0].fov,
			eyeSwapchains[0].CurrentSwapchainSubImage(),
		},
		{
			XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			nullptr,
			renderViews[1].pose,
			renderViews[1].fov,
			eyeSwapchains[1].CurrentSwapchainSubImage(),
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
		uiSwapchain.CurrentSwapchainSubImage(),
		{ {0, 0, 0, 1}, {0, vr_uiOverlayVerticalOffset.GetFloat(), -vr_uiOverlayDistance.GetFloat()} },
		{ vr_uiOverlayHeight.GetFloat() * vr_uiOverlayAspect.GetFloat(), vr_uiOverlayHeight.GetFloat() },
	};
	const XrCompositionLayerBaseHeader * const submittedLayers[] = {
		reinterpret_cast< const XrCompositionLayerBaseHeader* const >( &stereoLayer ),
		reinterpret_cast< const XrCompositionLayerBaseHeader* const >( &uiLayer ),
	};
	XrFrameEndInfo frameEndInfo = {
		XR_TYPE_FRAME_END_INFO,
		nullptr,
		currentFrameDisplayTime,
		XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		sizeof(submittedLayers) / sizeof(submittedLayers[0]),
		submittedLayers,
	};
	XrResult result = xrEndFrame( session, &frameEndInfo );
	XR_CheckResult( result, "submitting frame", instance, false );
}

void OpenXRBackend::GetFov( int eye, float &angleLeft, float &angleRight, float &angleUp, float &angleDown ) {
	const XrFovf &fov = renderViews[eye].fov;
	angleLeft = fov.angleLeft;
	angleRight = fov.angleRight;
	angleUp = fov.angleUp;
	angleDown = fov.angleDown;
}

bool OpenXRBackend::GetCurrentEyePose( int eye, idVec3 &origin, idMat3 &axis ) {
	if ( !vrSessionActive ) {
		return false;
	}

	const XrPosef &pose = renderViews[eye].pose;
	origin = Vec3FromXr( pose.position );
	axis = QuatFromXr( pose.orientation ).ToMat3();

	return true;
}

void OpenXRBackend::AcquireFboAndTexture( eyeView_t eye, FrameBuffer *&fbo, idImage *&texture ) {
	if ( eye == UI ) {
		uiSwapchain.PrepareNextImage();
		fbo = uiSwapchain.CurrentFrameBuffer();
		texture = uiSwapchain.CurrentImage();
	} else {
		eyeSwapchains[eye].PrepareNextImage();
		fbo = eyeSwapchains[eye].CurrentFrameBuffer();
		texture = eyeSwapchains[eye].CurrentImage();
	}
}

idList<idVec2> OpenXRBackend::GetHiddenAreaMask( eyeView_t eye ) {
	return idList<idVec2>();
}

void OpenXRBackend::AdjustRenderView( renderView_t *view ) {
	XrViewLocateInfo locateInfo = {
		XR_TYPE_VIEW_LOCATE_INFO,
		nullptr,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		nextFrameDisplayTime,
		seatedSpace,
	};
	XrViewState viewState = { XR_TYPE_VIEW_STATE, nullptr };
	XrView views[2] = { { XR_TYPE_VIEW, nullptr }, { XR_TYPE_VIEW, nullptr }};
	uint32_t viewCount = 0;
	XrResult result = xrLocateViews( session, &locateInfo, &viewState, 2, &viewCount, views );
	if ( !XR_SUCCEEDED( result ) ) {
		return;
	}

	// locate the center point of the two eye views, which we'll use in the frontend to determine the contents
	// of the view
	idVec3 hmdPosition = ( Vec3FromXr( views[0].pose.position ) + Vec3FromXr( views[1].pose.position ) ) * .5f;
	idQuat hmdOrientation = ( QuatFromXr( views[0].pose.orientation ) + QuatFromXr( views[1].pose.orientation ) ) * .5f;
	hmdOrientation.Normalize();

	// adjust view axis and origin
	view->initialVieworg = view->vieworg;
	view->initialViewaxis = view->viewaxis;
	view->fixedOrigin = false;
	view->vieworg += hmdPosition * view->viewaxis;
	view->viewaxis = hmdOrientation.ToMat3() * view->viewaxis;

	// set horizontal and vertical fov
	// for the scene collection in the frontend, we'll use a symmetric FoV, with angles slidely widened
	// to create a bit of a buffer in the frustum, since we are going to readjust the view in the actual
	// rendering for the eyes
	float leftFovX = 2 * Max( idMath::Fabs( views[0].fov.angleLeft ), idMath::Fabs( views[0].fov.angleRight ) );
	float rightFovX = 2 * Max( idMath::Fabs( views[1].fov.angleLeft ), idMath::Fabs( views[1].fov.angleRight ) );
	float leftFovY = 2 * Max( idMath::Fabs( views[0].fov.angleUp ), idMath::Fabs( views[0].fov.angleDown ) );
	float rightFovY = 2 * Max( idMath::Fabs( views[1].fov.angleUp ), idMath::Fabs( views[1].fov.angleDown ) );
	view->fov_x = RAD2DEG( Max( leftFovX, rightFovX ) ) + 5;
	view->fov_y = RAD2DEG( Max( leftFovY, rightFovY ) ) + 5;
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
	idList<int64_t> desiredFormats = { GL_SRGB8_ALPHA8, GL_RGBA8, GL_RGBA16F };
	idList<idStr> formatNames = { "GL_SRGB8_ALPHA8", "GL_RGBA8", "GL_RGBA16F" };
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
	
	eyeSwapchains[0].Init( "leftEye", swapchainFormat, views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight );
	eyeSwapchains[1].Init( "rightEye", swapchainFormat, views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight );
	uiSwapchain.Init( "ui", swapchainFormat, vr_uiResolution.GetInteger(), vr_uiResolution.GetInteger() );

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
