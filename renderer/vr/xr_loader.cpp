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
#include "xr_loader.h"

idCVar vr_useDebug( "vr_useDebug", "0", CVAR_RENDERER|CVAR_BOOL|CVAR_ARCHIVE, "Enable OpenXR debug functionality" );

bool XR_KHR_opengl_enable_available = false;
bool XR_KHR_visibility_mask_available = false;
bool XR_EXT_debug_utils_available = false;

PFN_xrGetOpenGLGraphicsRequirementsKHR qxrGetOpenGLGraphicsRequirementsKHR = nullptr;

// -- xr function prototype implementations for dynamically loaded extension functions
XRAPI_ATTR XrResult XRAPI_CALL xrGetOpenGLGraphicsRequirementsKHR(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrGraphicsRequirementsOpenGLKHR*            graphicsRequirements) {
	return qxrGetOpenGLGraphicsRequirementsKHR( instance, systemId, graphicsRequirements );	
}
// -----------------------------------------------------------------------------------

void XR_CheckResult( XrResult result, const char *description, XrInstance instance ) {
	if ( XR_SUCCEEDED( result ) ) {
		return;
	}

	char resultString[XR_MAX_RESULT_STRING_SIZE];
	xrResultToString( instance, result, resultString );
	common->FatalError( "OpenXR call failed - %s: %s", description, resultString );
}

void XR_CheckAvailableExtensions() {
	uint32_t extensionsCount = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties( nullptr, 0, &extensionsCount, nullptr );
	XR_CheckResult( result, "querying number of available extensions" );

	idList<XrExtensionProperties> extensionProperties;
	extensionProperties.SetNum( extensionsCount );
	for ( auto &ext : extensionProperties ) {
		ext.type = XR_TYPE_EXTENSION_PROPERTIES;
		ext.next = nullptr;
	}
	result = xrEnumerateInstanceExtensionProperties( nullptr, extensionProperties.Num(), &extensionsCount, extensionProperties.Ptr() );
	XR_CheckResult( result, "querying available extensions" );

	common->Printf( "Supported extensions:\n" );
	for ( auto ext : extensionProperties ) {
		common->Printf( "- %s (v%d.%d.%d)\n", ext.extensionName,
			XR_VERSION_MAJOR( ext.extensionVersion ),
			XR_VERSION_MINOR( ext.extensionVersion ),
			XR_VERSION_PATCH( ext.extensionVersion ) );

		if ( strcmp( ext.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME ) == 0 ) {
			XR_KHR_opengl_enable_available = true;
		}
		if ( strcmp( ext.extensionName, XR_KHR_VISIBILITY_MASK_EXTENSION_NAME ) == 0 ) {
			XR_KHR_visibility_mask_available = true;
		}
		if ( strcmp( ext.extensionName, XR_EXT_DEBUG_UTILS_EXTENSION_NAME ) == 0 ) {
			XR_EXT_debug_utils_available = true;
		}
	}
}

void XR_LoadExtensionOpenGL( XrInstance instance ) {
	XrResult result;
	result = xrGetInstanceProcAddr( instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&qxrGetOpenGLGraphicsRequirementsKHR );
	XR_CheckResult( result, "loading OpenGL extension function", instance );
}

XrInstance XR_CreateInstance() {
	XR_KHR_opengl_enable_available = false;
	XR_KHR_visibility_mask_available = false;
	XR_EXT_debug_utils_available = false;
	XR_CheckAvailableExtensions();
	if ( !XR_KHR_opengl_enable_available ) {
		common->FatalError( "XR_KHR_opengl_enable extension is required, but not supported" );
	}
	if ( !vr_useDebug.GetBool() ) {
		XR_EXT_debug_utils_available = false;
	}

	idList<const char *> enabledExtensions = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
	if ( XR_KHR_visibility_mask_available ) {
		enabledExtensions.AddGrow( XR_KHR_VISIBILITY_MASK_EXTENSION_NAME );
	}
	if ( XR_EXT_debug_utils_available ) {
		enabledExtensions.AddGrow( XR_EXT_DEBUG_UTILS_EXTENSION_NAME );
	}
	XrInstanceCreateInfo instanceCreateInfo = {
		XR_TYPE_INSTANCE_CREATE_INFO,
		nullptr,
		0,
		{
			"The Dark Mod",
            (2 << 16) | 9,
			"",
			0,
			XR_CURRENT_API_VERSION,
		},
		0,
		nullptr,
		enabledExtensions.Num(),
		enabledExtensions.Ptr(),
	};
	XrInstance instance;
	XrResult result = xrCreateInstance( &instanceCreateInfo, &instance );
	XR_CheckResult( result, "creating the instance" );

	XrInstanceProperties instanceProperties = {
		XR_TYPE_INSTANCE_PROPERTIES,
		nullptr,
	};
	result = xrGetInstanceProperties( instance, &instanceProperties );
	XR_CheckResult( result, "querying instance properties", instance );
	common->Printf( "OpenXR runtime: %s (v%d.%d.%d)\n", instanceProperties.runtimeName,
		XR_VERSION_MAJOR( instanceProperties.runtimeVersion ),
		XR_VERSION_MINOR( instanceProperties.runtimeVersion ),
		XR_VERSION_PATCH( instanceProperties.runtimeVersion ) );

	XR_LoadExtensionOpenGL( instance );

	return instance;
}
