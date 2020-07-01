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

#define XR_USE_GRAPHICS_API_OPENGL 1
#ifdef WIN32
#define XR_USE_PLATFORM_WIN32 1
#endif
#ifdef __linux__
#define XR_USE_PLATFORM_XLIB 1
#endif
#include <openxr/openxr_platform.h>

extern bool XR_KHR_opengl_enable_available;
extern bool XR_KHR_visibility_mask_available;
extern bool XR_EXT_debug_utils_available;

void XR_CheckResult( XrResult result, const char *description, XrInstance instance = nullptr );
XrInstance XR_CreateInstance();

