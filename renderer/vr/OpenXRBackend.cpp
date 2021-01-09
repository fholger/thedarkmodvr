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
#include "../Profiling.h"

OpenXRBackend xrImpl;
OpenXRBackend *xrBackend = &xrImpl;

idCVar xr_preferD3D11 ( "xr_preferD3D11", "1", CVAR_RENDERER|CVAR_BOOL|CVAR_ARCHIVE, "Use D3D11 for OpenXR session to work around a performance issue with SteamVR's OpenXR implementation" );

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
#include "../../sys/linux/local.h"
typedef XrGraphicsBindingOpenGLXlibKHR XrGraphicsBindingGL;
XrGraphicsBindingOpenGLXlibKHR Sys_CreateGraphicsBindingGL() {
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

void OpenXRBackend::UpdateInput( int axis[6], idList<padActionChange_t> &actionChanges ) {
	input.UpdateInput( axis, actionChanges );
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
