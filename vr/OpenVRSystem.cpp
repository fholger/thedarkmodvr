#include "precompiled_engine.h"
#include "OpenVRSystem.h"
#include "openvr.h"


OpenVRSystem::OpenVRSystem(): vrSystem(nullptr)
{
}


OpenVRSystem::~OpenVRSystem()
{
	Shutdown();
}

bool OpenVRSystem::Init()
{
	if (!vr::VR_IsHmdPresent())
	{
		common->Printf("No OpenVR HMD detected.\n");
		return false;
	}

	vr::EVRInitError initError = vr::VRInitError_None;
	vrSystem = vr::VR_Init(&initError, vr::VRApplication_Scene);
	if (initError != vr::VRInitError_None)
	{
		vrSystem = nullptr;
		common->Printf("OpenVR initialization failed: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(initError));
		return false;
	}

	if (!vr::VRCompositor())
	{
		common->Printf("OpenVR compositor initialization failed.\n");
		return false;
	}

	return true;
}

void OpenVRSystem::Shutdown()
{
	if (vrSystem != nullptr)
	{
		common->Printf("Shutting down OpenVR.");
		vr::VR_Shutdown();
		vrSystem = nullptr;
	}
}
