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
#include <openxr/openxr_platform.h>

VrBackend vrImpl;
VrBackend *vr = &vrImpl;

namespace {
	void CheckXrResult( XrInstance instance, XrResult result ) {
		if ( XR_SUCCEEDED( result ) ) {
			return;
		}

		char resultString[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString( instance, result, resultString );
		common->FatalError( "Failed to initialize OpenXR: %s", resultString );
	}
}

void VrBackend::Init() {
	if ( instance != nullptr ) {
		Destroy();
	}

	common->Printf( "Initializing VR subsystem\n" );

	const char * const enabledExtensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo instanceCreateInfo = {
		XR_TYPE_INSTANCE_CREATE_INFO,
		nullptr,
		0,
		{
			"The Dark Mod",
			209,
			"",
			0,
			XR_CURRENT_API_VERSION,
		},
		0,
		nullptr,
		1,
		enabledExtensions,
	};
	XrResult result = xrCreateInstance( &instanceCreateInfo, &instance );
	CheckXrResult( instance, result );
	common->Printf( "...Created OpenXR instance\n" );

	XrInstanceProperties instanceProperties = {
		XR_TYPE_INSTANCE_PROPERTIES,
		nullptr,
	};
	result = xrGetInstanceProperties( instance, &instanceProperties );
	CheckXrResult( instance, result );
	common->Printf( "...OpenXR runtime: %s (v%d.%d.%d)\n", instanceProperties.runtimeName,
		XR_VERSION_MAJOR( instanceProperties.runtimeVersion ),
		XR_VERSION_MINOR( instanceProperties.runtimeVersion ),
		XR_VERSION_PATCH( instanceProperties.runtimeVersion ) );
}

void VrBackend::Destroy() {
	if ( instance == nullptr ) {
		return;
	}

	xrDestroyInstance( instance );
	instance = nullptr;
}
