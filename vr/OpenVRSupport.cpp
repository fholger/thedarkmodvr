#include "precompiled_engine.h"
#include "OpenVRSupport.h"
#include <openvr.h>

OpenVRSupport vrLocal;
OpenVRSupport* vrSupport = &vrLocal;

OpenVRSupport::OpenVRSupport(): vrSystem(nullptr)
{
}

OpenVRSupport::~OpenVRSupport()
{
}

void OpenVRSupport::Init()
{
	if (!vr::VR_IsHmdPresent())
	{
		common->Printf("No OpenVR HMD detected.\n");
		return;
	}

	vr::EVRInitError initError = vr::VRInitError_None;
	vrSystem = vr::VR_Init(&initError, vr::VRApplication_Scene);
	if (initError != vr::VRInitError_None)
	{
		vrSystem = nullptr;
		common->Printf("OpenVR initialization failed: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(initError));
		return;
	}

	if (!vr::VRCompositor())
	{
		common->Printf("OpenVR compositor initialization failed.\n");
		Shutdown();
		return;
	}
}

void OpenVRSupport::Shutdown()
{
	if (vrSystem != nullptr)
	{
		common->Printf("Shutting down OpenVR.");
		vr::VR_Shutdown();
		vrSystem = nullptr;
	}
}
