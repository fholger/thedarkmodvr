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
#include "VrBackend.h"
#include "xr_loader.h"
#include "../tr_local.h"
#include "../FrameBufferManager.h"
#include "../FrameBuffer.h"

VrBackend vrImpl;
VrBackend *vr = &vrImpl;

idCVar vr_uiResolution( "vr_uiResolution", "2048", CVAR_RENDERER|CVAR_ARCHIVE|CVAR_INTEGER, "Render resolution for 2D/UI overlay" );

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

void VrBackend::Init() {
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

void VrBackend::Destroy() {
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

void VrBackend::BeginFrame() {
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
	XrFrameState frameState;
	result = xrWaitFrame( session, nullptr, &frameState );
	XR_CheckResult( result, "awaiting frame", instance );
	shouldSubmitFrame = frameState.shouldRender == XR_TRUE;
	currentFrameDisplayTime = frameState.predictedDisplayTime;
	nextFrameDisplayTime = currentFrameDisplayTime + frameState.predictedDisplayPeriod;

	result = xrBeginFrame( session, nullptr );
	XR_CheckResult( result, "beginning frame", instance );
}

void VrBackend::EndFrame() {
	if ( !vrSessionActive || !shouldSubmitFrame ) {
		return;
	}

	// FIXME: this is just a quick hack to get some render output to the headset
	uiSwapchain.PrepareNextImage();
	frameBuffers->defaultFbo->BlitTo( uiSwapchain.CurrentFrameBuffer(), GL_COLOR_BUFFER_BIT, GL_LINEAR );
	qglFlush();
	uiSwapchain.ReleaseImage();
	
	XrCompositionLayerQuad uiLayer = {
		XR_TYPE_COMPOSITION_LAYER_QUAD,
		nullptr,
		XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
		seatedSpace,
		XR_EYE_VISIBILITY_BOTH,
		uiSwapchain.CurrentSwapchainSubImage(),
		{ {0, 0, 0, 1}, {0, 0, -2} },
		{ 2, 1.6f },
	};
	const XrCompositionLayerBaseHeader * const submittedLayers[] = {
		reinterpret_cast< const XrCompositionLayerBaseHeader* const >( &uiLayer ),
	};
	XrFrameEndInfo frameEndInfo = {
		XR_TYPE_FRAME_END_INFO,
		nullptr,
		currentFrameDisplayTime,
		XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		//sizeof(submittedLayers) / sizeof(submittedLayers[0]),
		//submittedLayers,
		0,
		nullptr,
	};
	XrResult result = xrEndFrame( session, &frameEndInfo );
	XR_CheckResult( result, "submitting frame", instance, false );
}

void VrBackend::SetupDebugMessenger() {
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

void VrBackend::ChooseSwapchainFormat() {
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
	idList<int64_t> desiredFormats = { GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGBA16F };
	idList<idStr> formatNames = { "GL_RGBA8", "GL_SRGB8_ALPHA8", "GL_RGBA16F" };
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

void VrBackend::InitSwapchains() {
	ChooseSwapchainFormat();

	uint32_t numViews = 0;
	XrResult result = xrEnumerateViewConfigurationViews( instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		2, &numViews, views );
	XR_CheckResult( result, "getting view info", instance );
	common->Printf( "Recommended render resolution: %dx%d\n", views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight );
	
	eyeSwapchains[0].Init( "leftEye", swapchainFormat, views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight );
	eyeSwapchains[1].Init( "rightEye", swapchainFormat, views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight );
	uiSwapchain.Init( "ui", swapchainFormat, vr_uiResolution.GetInteger(), vr_uiResolution.GetInteger() );

	common->Printf( "Created swapchains\n" );
}

void VrBackend::HandleSessionStateChange( const XrEventDataSessionStateChanged &stateChangedEvent ) {
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
		vrSessionActive = true;
		break;

	case XR_SESSION_STATE_IDLE:
		vrSessionActive = false;
		break;

	case XR_SESSION_STATE_LOSS_PENDING:
	case XR_SESSION_STATE_STOPPING:
		common->FatalError( "xr Session shutting down or lost" );
	}	
}