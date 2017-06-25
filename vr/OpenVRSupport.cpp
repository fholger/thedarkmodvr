#include "precompiled_engine.h"
#include "OpenVRSupport.h"
#include <openvr.h>

class OpenVRSupportLocal : public OpenVRSupport {
public:
	OpenVRSupportLocal();
	~OpenVRSupportLocal() override;
	
	void Init() override;
	void Shutdown() override;
	bool IsInitialized() const override;
	float GetInterPupillaryDistance() const override;

private:
	vr::IVRSystem *vrSystem;
};

OpenVRSupportLocal vrLocal;
OpenVRSupport* vrSupport = &vrLocal;

OpenVRSupportLocal::OpenVRSupportLocal(): vrSystem(nullptr)
{
}

OpenVRSupportLocal::~OpenVRSupportLocal()
{
}

void OpenVRSupportLocal::Init()
{
	common->Printf( "Initializing OpenVR support...\n" );
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
	common->Printf( "OpenVR support ready.\n" );
}

void OpenVRSupportLocal::Shutdown()
{
	if (vrSystem != nullptr)
	{
		common->Printf("Shutting down OpenVR.");
		vr::VR_Shutdown();
		vrSystem = nullptr;
	}
}

bool OpenVRSupportLocal::IsInitialized() const {
	return vrSystem != nullptr;
}

float OpenVRSupportLocal::GetInterPupillaryDistance() const {
	return vrSystem->GetFloatTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float ) * 100;
}
