#include "precompiled_engine.h"
#include "VrSupport.h"
#include <openvr.h>
#include "../renderer/Image.h"

class OpenVrSupport : public VrSupport {
public:
	OpenVrSupport();
	~OpenVrSupport() override;
	
	void Init() override;
	void Shutdown() override;
	bool IsInitialized() const override;
	float GetInterPupillaryDistance() const override;
	void DetermineRenderTargetSize( uint32_t *width, uint32_t *height ) const override;
	void SubmitEyeFrame( int eye, idImage* image ) override;
	void FrameStart() override;

private:
	vr::IVRSystem *vrSystem;
	vr::TrackedDevicePose_t trackedDevicePose[vr::k_unMaxTrackedDeviceCount];
	float fovX;
	float fovY;
	float aspect;

	void InitParameters();
};

OpenVrSupport vrLocal;
VrSupport* vrSupport = &vrLocal;

OpenVrSupport::OpenVrSupport(): vrSystem(nullptr), fovX(0), fovY(0), aspect(0) {
}

OpenVrSupport::~OpenVrSupport()
{
}

void OpenVrSupport::Init()
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
	vr::VRCompositor()->SetTrackingSpace( vr::TrackingUniverseSeated );
	common->Printf( "OpenVR support ready.\n" );
}

void OpenVrSupport::Shutdown()
{
	if (vrSystem != nullptr)
	{
		common->Printf("Shutting down OpenVR.");
		vr::VR_Shutdown();
		vrSystem = nullptr;
	}
}

bool OpenVrSupport::IsInitialized() const {
	return vrSystem != nullptr;
}

float OpenVrSupport::GetInterPupillaryDistance() const {
	return vrSystem->GetFloatTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_UserIpdMeters_Float ) * 100;
}

void OpenVrSupport::DetermineRenderTargetSize( uint32_t* width, uint32_t* height ) const {
	vrSystem->GetRecommendedRenderTargetSize( width, height );
}

void OpenVrSupport::SubmitEyeFrame( int eye, idImage* image ) {
	vr::Texture_t texture = { (void*)image->texnum, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
	vr::EVRCompositorError error = vr::VRCompositor()->Submit(eye == LEFT_EYE ? vr::Eye_Left : vr::Eye_Right, &texture);
	if (error != vr::VRCompositorError_None) {
		common->Warning( "Submitting eye texture failed: %d", error );
	}
}

void OpenVrSupport::FrameStart() {
	vr::VRCompositor()->WaitGetPoses( trackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0 );
}

void OpenVrSupport::InitParameters() {
	float proj[2][4];
	vrSystem->GetProjectionRaw( vr::Eye_Left, &proj[0][0], &proj[0][1], &proj[0][2], &proj[0][3] );
	vrSystem->GetProjectionRaw( vr::Eye_Right, &proj[1][0], &proj[1][1], &proj[1][2], &proj[1][3] );
	float combinedTanHalfFovHoriz = max( max( proj[0][0], proj[0][1] ), max( proj[1][0], proj[1][1] ) );
	float combinedTanHalfFovVert = max( max( proj[0][2], proj[0][3] ), max( proj[1][2], proj[1][3] ) );

	fovX = RAD2DEG( 2 * atanf( combinedTanHalfFovHoriz ) );
	fovY = RAD2DEG( 2 * atanf( combinedTanHalfFovVert ) );
	aspect = combinedTanHalfFovHoriz / combinedTanHalfFovVert;
}
