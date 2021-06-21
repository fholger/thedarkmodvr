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

// The following macros define the minimum required platform.  The minimum required platform
// is the earliest version of Windows, Internet Explorer etc. that has the necessary features to run 
// your application.  The macros work by enabling all features available on platform versions up to and 
// including the version specified.

// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
//#ifndef WINVER                          // Specifies that the minimum required platform is Windows Vista.
//#define WINVER 0x0600           // Change this to the appropriate value to target other versions of Windows.
//#endif

#ifndef _WIN32_WINNT                        // Specifies that the minimum required platform is Windows Vista.
#define _WIN32_WINNT _WIN32_WINNT_VISTA     // Change this to the appropriate value to target other versions of Windows.
#endif

//#ifndef _WIN32_WINDOWS          // Specifies that the minimum required platform is Windows 98.
//#define _WIN32_WINDOWS 0x0410 // Change this to the appropriate value to target Windows Me or later.
//#endif

//#ifndef _WIN32_IE                       // Specifies that the minimum required platform is Internet Explorer 7.0.
//#define _WIN32_IE 0x0700        // Change this to the appropriate value to target other versions of IE.
//#endif

