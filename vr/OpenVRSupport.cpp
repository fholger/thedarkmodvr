#include "precompiled_engine.h"
#include "VrSupport.h"
#include <openvr.h>
#include "../renderer/Image.h"
#include "../renderer/tr_local.h"

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
	void SetupProjectionMatrix( viewDef_t* viewDef ) override;
	void GetFov( float& fovX, float& fovY ) override;
private:
	vr::IVRSystem *vrSystem;
	vr::TrackedDevicePose_t trackedDevicePose[vr::k_unMaxTrackedDeviceCount];
	float rawProjection[2][4];
	float fovX;
	float fovY;
	float aspect;

	void InitParameters();
};

OpenVrSupport vrLocal;
VrSupport* vrSupport = &vrLocal;

OpenVrSupport::OpenVrSupport(): vrSystem(nullptr) {
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
	InitParameters();
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

void OpenVrSupport::SetupProjectionMatrix( viewDef_t* viewDef ) {
	const float zNear = (viewDef->renderView.cramZNear) ? (r_znear.GetFloat() * 0.25f) : r_znear.GetFloat();
	const int eye = viewDef->renderView.viewEyeBuffer == LEFT_EYE ? 0 : 1;
	const float idx = 1.0f / (rawProjection[eye][1] - rawProjection[eye][0]);
	const float idy = 1.0f / (rawProjection[eye][3] - rawProjection[eye][2]);
	const float sx = rawProjection[eye][0] + rawProjection[eye][1];
	const float sy = rawProjection[eye][2] + rawProjection[eye][3];

	viewDef->projectionMatrix[0 * 4 + 0] = 2.0f * idx;
	viewDef->projectionMatrix[1 * 4 + 0] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 0] = sx * idx;
	viewDef->projectionMatrix[3 * 4 + 0] = 0.0f;

	viewDef->projectionMatrix[0 * 4 + 1] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 1] = 2.0f * idy;
	viewDef->projectionMatrix[2 * 4 + 1] = sy*idy;	
	viewDef->projectionMatrix[3 * 4 + 1] = 0.0f;

	viewDef->projectionMatrix[0 * 4 + 2] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 2] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 2] = -0.999f; 
	viewDef->projectionMatrix[3 * 4 + 2] = -2.0f * zNear;

	viewDef->projectionMatrix[0 * 4 + 3] = 0.0f;
	viewDef->projectionMatrix[1 * 4 + 3] = 0.0f;
	viewDef->projectionMatrix[2 * 4 + 3] = -1.0f;
	viewDef->projectionMatrix[3 * 4 + 3] = 0.0f;
}

void OpenVrSupport::GetFov( float& fovX, float& fovY ) {
	fovX = this->fovX;
	fovY = this->fovY;
}

void OpenVrSupport::InitParameters() {
	vrSystem->GetProjectionRaw( vr::Eye_Left, &rawProjection[0][0], &rawProjection[0][1], &rawProjection[0][2], &rawProjection[0][3] );
	vrSystem->GetProjectionRaw( vr::Eye_Right, &rawProjection[1][0], &rawProjection[1][1], &rawProjection[1][2], &rawProjection[1][3] );
	float combinedTanHalfFovHoriz = max( max( rawProjection[0][0], rawProjection[0][1] ), max( rawProjection[1][0], rawProjection[1][1] ) );
	float combinedTanHalfFovVert = max( max( rawProjection[0][2], rawProjection[0][3] ), max( rawProjection[1][2], rawProjection[1][3] ) );

	fovX = RAD2DEG( 2 * atanf( combinedTanHalfFovHoriz ) );
	fovY = RAD2DEG( 2 * atanf( combinedTanHalfFovVert ) );
	aspect = combinedTanHalfFovHoriz / combinedTanHalfFovVert;
}
