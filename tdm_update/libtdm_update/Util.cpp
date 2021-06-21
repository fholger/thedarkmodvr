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

#include "Util.h"

#include "StdFilesystem.h"
#include "StdString.h"
#include "TraceLog.h"

namespace fs = stdext;

#ifdef WIN32

#include <string>
#include <windows.h>
#include "Psapi.h"

namespace tdm
{

bool Util::TDMIsRunning()
{
	DWORD processes[1024];
	DWORD num;

	if (!EnumProcesses(processes, sizeof(processes), &num))
	{
		return false;
	}

	// Iterate over the processes
	for (int i = 0; i < int(num/sizeof(DWORD)); i++)
	{
		char szProcessName[MAX_PATH] = "unknown";

		// Get the handle for this process
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, processes[i]);

		if (hProcess)
		{
			HMODULE hMod;
			DWORD countBytes;

			if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &countBytes))
			{
				GetModuleBaseName(hProcess, hMod, szProcessName, sizeof(szProcessName));

				std::string processName(szProcessName);

				// grayman - This was checking for "Doom3.exe". Starting with 1.08, the D3
				// executable is no longer needed to run TDM, so we'll check for TheDarkMod.exe
				// instead.

				if (processName == "TheDarkMod.exe")
				{
					// At this point, we know we need to quit. There's no longer a need
					// to check for "gamex86.dll".

/*					HMODULE hModules[1024];
					DWORD cbNeeded;

					if (EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded))
					{
						for (unsigned int m = 0; m < (cbNeeded / sizeof(HMODULE)); m++)
						{
							TCHAR szModName[MAX_PATH];

							// Get the full path to the module's file.
							if (GetModuleBaseName(hProcess, hModules[m], szModName, sizeof(szModName)/sizeof(TCHAR)))
							{
								// Print the module name and handle value.
								if (std::string(szModName) == "gamex86.dll")
								{
									CloseHandle(hProcess); // close the handle, we're terminating

									return true;
								}
							}
						}
					}
 */
					// instead, quit

					CloseHandle(hProcess); // close the handle, we're terminating

					return true;
				}
			}
		}

		CloseHandle(hProcess);
	}

	return false;
}

bool Util::DarkRadiantIsRunning()
{
	DWORD processes[1024];
	DWORD num;

	if (!EnumProcesses(processes, sizeof(processes), &num))
	{
		return false;
	}

	// Iterate over the processes
	for (int i = 0; i < int(num/sizeof(DWORD)); i++)
	{
		char szProcessName[MAX_PATH] = "unknown";

		// Get the handle for this process
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, processes[i]);

		if (hProcess)
		{
			HMODULE hMod;
			DWORD countBytes;

			if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &countBytes))
			{
				GetModuleBaseName(hProcess, hMod, szProcessName, sizeof(szProcessName));

				std::string processName(szProcessName);
				processName = stdext::to_lower_copy(processName);

				if (processName == "darkradiant.exe")
				{
					CloseHandle(hProcess); // close the handle, we're terminating
					return true;
				}
			}
		}

		CloseHandle(hProcess);
	}

	return false;
}

bool Util::HasElevatedPrivilegesWindows() {
	bool underAdmin = false;
	HANDLE hProcess = GetCurrentProcess();
	HANDLE hToken;
	if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
		// Get the Integrity level.
		DWORD dwLengthNeeded;
		if (!GetTokenInformation(hToken, TokenIntegrityLevel, NULL, 0, &dwLengthNeeded)) {
			PTOKEN_MANDATORY_LABEL pTIL = (PTOKEN_MANDATORY_LABEL)malloc(dwLengthNeeded);
			if (GetTokenInformation(hToken, TokenIntegrityLevel, pTIL, dwLengthNeeded, &dwLengthNeeded)) {
				DWORD dwIntegrityLevel = *GetSidSubAuthority(pTIL->Label.Sid, (DWORD)(UCHAR)(*GetSidSubAuthorityCount(pTIL->Label.Sid)-1));
				if (dwIntegrityLevel >= SECURITY_MANDATORY_HIGH_RID) {
					underAdmin = true;
				}
			}
			free(pTIL);
		}
		CloseHandle(hToken);
	}
	return underAdmin;
}

} // namespace

#elif defined(__linux__) || defined(__freebsd__)
// Linux/FreeBSD implementation

#include <iostream>
#include <fstream>
#include <string>

namespace tdm
{

namespace
{
	const std::string PROC_FOLDER("/proc/");
	const std::string TDM_PROCESS_NAME("thedarkmod.x86"); // grayman - looking for tdm now instead of doom3 
    const std::string TDM_PROCESS_NAME_X64("thedarkmod.x64");

	bool CheckProcessFile(const std::string& name, const std::string& processName)
	{
		// Try to cast the filename to an integer number (=PID)
		try
		{
			unsigned long pid = std::stoul(name);
		
			// Was the PID read correctly?
			if (pid == 0)
			{
				return false;
			}
			
			const std::string cmdLineFileName = PROC_FOLDER + name + "/cmdline";
			
			std::ifstream cmdLineFile(cmdLineFileName.c_str());

			if (cmdLineFile.is_open())
			{
				// Read the command line from the process file
				std::string cmdLine;
				getline(cmdLineFile, cmdLine);
				
				if (cmdLine.find(processName) != std::string::npos)
				{
					// Process found, return success
					return true;
				}
			}
			
			// Close the file
			cmdLineFile.close();
		}
		catch (std::invalid_argument&)
		{
			// Cast to int failed, no PID
		}

		return false;
	}

} // namespace

bool Util::TDMIsRunning()
{
	// Traverse the /proc folder, this sets the flag to TRUE if the process was found
	for (fs::path file : fs::directory_enumerate(PROC_FOLDER))
	{
		if (CheckProcessFile(file.filename().string(), TDM_PROCESS_NAME) ||
            CheckProcessFile(file.filename().string(), TDM_PROCESS_NAME_X64)) // grayman - looking for tdm now instead of doom3
		{
			return true;
		}
	}
	
	return false;
}

bool Util::DarkRadiantIsRunning()
{
	// Traverse the /proc folder, this sets the flag to TRUE if the process was found
	for (fs::path file : fs::directory_enumerate(PROC_FOLDER))
	{
		if (CheckProcessFile(file.filename().string(), "darkradiant"))
		{
			return true;
		}
	}
	
	return false;
}

bool Util::HasElevatedPrivilegesWindows() {
	return false;
}

} // namespace

#elif defined(MACOS_X)
// Mac OS X
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/sysctl.h>

namespace tdm
{

// greebo: Checks for a named process, modeled loosely after
// http://developer.apple.com/library/mac/#qa/qa2001/qa1123.html
bool FindProcessByName(const char* processName)
{
    int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t length = 0;
	
	// Call sysctl with a NULL buffer.
	int err = sysctl(name, 4, NULL, &length, NULL, 0);
	
	if (err == -1)
	{
		TraceLog::WriteLine(LOG_ERROR, "Failed to receive buffer size for process list.");
		return false;
	}
	
    kinfo_proc* procList = static_cast<kinfo_proc*>(malloc(length));
	
	if (procList == NULL)
	{
		TraceLog::WriteLine(LOG_ERROR, "Out of Memory trying to allocate process buffer");
		return false;
	}
	
	// Load process info
	sysctl(name, 4, procList, &length, NULL, 0);
	
	size_t procCount = length / sizeof(kinfo_proc);
	bool result = false;
	
	for (size_t i = 0; i < procCount; ++i)
	{
		//TraceLog::WriteLine(LOG_STANDARD, procList[i].kp_proc.p_comm);
		
		if (strcmp(procList[i].kp_proc.p_comm, processName) == 0)
		{
			result = true;
			break;
		}
	}
	
	free(procList);
	
	return result;
}

bool Util::TDMIsRunning()
{
	return FindProcessByName("thedarkmod"); // grayman - look for TDM instead of "Doom 3".
}

bool Util::DarkRadiantIsRunning()
{
	// DarkRadiant isn't existing in Mac so far
	return FindProcessByName("DarkRadiant");
}

} // namespace

#else
#error Unsupported Platform
#endif
