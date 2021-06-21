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

namespace tdm
{

const char* const LIBTDM_UPDATE_VERSION = "0.77";

const char* const LIBTDM_UPDATE_PLATFORM = 
#ifdef WIN32
"Win32"
#elif defined(__linux__)
"Linux"
#elif defined(MACOS_X)
"MacOSX"
#else
"Unknown"
#endif
;

const char* const TDM_MIRRORS_FILE = "tdm_mirrors.txt";
const char* const TDM_MIRRORS_SERVERS[] = { 
	"http://mirrors.thedarkmod.com/",
	"http://www.thedarkmod.com/update/",
	""
};

// The file containing the version information of the most up-to-date package
const char* const TDM_CRC_INFO_FILE = "crc_info.txt";
const char* const TDM_UDPATE_INFO_FILE = "tdm_update_info.txt";

// The file containing the version information of all released packages since 1.02
const char* const TDM_VERSION_INFO_FILE = "tdm_version_info.txt";

#ifdef WIN32
const char* const TDM_UPDATE_UPDATER_BATCH_FILE = "tdm_update_updater.cmd";
#else
const char* const TDM_UPDATE_UPDATER_BATCH_FILE = "tdm_update_updater.sh";
#endif

// The standard mod path below D3
const char* const TDM_STANDARD_MOD_FOLDER = "darkmod";

// The standard manifest folder, relative to darkmod
const char* const TDM_MANIFEST_PATH = "devel/manifests/";
const char* const TDM_MANIFEST_EXTENSION = ".txt";

const char* const TMP_FILE_PREFIX = "__";

} // namespace
