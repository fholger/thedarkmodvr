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
	void GetHeadTracking( idVec3& headOrigin, idMat3& headAxis ) override;
	void AdjustViewWithPredictedHeadPose( renderView_t& eyeView, const int eye ) override;
	void AdjustViewWithActualHeadPose( viewDef_t* viewDef ) override;
	void FrameEnd() override;
private:
	void UpdateHmdOriginAndAxis( const vr::TrackedDevicePose_t devicePose[16], idVec3& origin, idMat3& axis );

	vr::IVRSystem *vrSystem;
	vr::TrackedDevicePose_t trackedDevicePose[vr::k_unMaxTrackedDeviceCount];
	vr::TrackedDevicePose_t predictedDevicePose[vr::k_unMaxTrackedDeviceCount];
	float rawProjection[2][4];
	float fovX;
	float fovY;
	float aspect;
	float eyeForward;
	idVec3 hmdOrigin;
	idMat3 hmdAxis;
	idVec3 predictedHmdOrigin;
	idMat3 predictedHmdAxis;
	float scale;

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
	common->Printf( "OpenVR: Recommended render target size %d x %d\n", *width, *height );
}

void OpenVrSupport::SubmitEyeFrame( int eye, idImage* image ) {
	glViewport( 0, 0, image->uploadWidth, image->uploadHeight );
	glScissor( 0, 0, image->uploadWidth, image->uploadHeight );
	vr::Texture_t texture = { (void*)image->texnum, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
	vr::EVRCompositorError error = vr::VRCompositor()->Submit(eye == LEFT_EYE ? vr::Eye_Left : vr::Eye_Right, &texture);
	if (error != vr::VRCompositorError_None) {
		common->Warning( "Submitting eye texture failed: %d", error );
	}
}

void OpenVrSupport::UpdateHmdOriginAndAxis( const vr::TrackedDevicePose_t devicePose[16], idVec3& origin, idMat3& axis ) {
	const vr::TrackedDevicePose_t &hmdPose = devicePose[vr::k_unTrackedDeviceIndex_Hmd];
	if (hmdPose.bPoseIsValid) {
		// OpenVR coordinate system:
		//   x is right
		//   y is up
		//  -z is forward
		//   distance in meters
		// Doom3 coordinate system
		//  x is forward
		//  y is left
		//  z is up
		//  distance in world units (1.1 inches)
		const vr::HmdMatrix34_t &mat = hmdPose.mDeviceToAbsoluteTracking;
		origin.Set( -scale * mat.m[2][3], -scale * mat.m[0][3], scale * mat.m[1][3] );
		axis[0].Set( mat.m[2][2], mat.m[0][2], -mat.m[1][2] );
		axis[1].Set( mat.m[2][0], mat.m[0][0], -mat.m[1][0] );
		axis[2].Set( -mat.m[2][1], -mat.m[0][1], mat.m[1][1] );
		origin += eyeForward * scale * axis[0];
	}
}

void OpenVrSupport::FrameStart() {
	vr::VRCompositor()->SetTrackingSpace( vr::TrackingUniverseSeated );
	vr::VRCompositor()->WaitGetPoses( trackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0 );
	UpdateHmdOriginAndAxis( trackedDevicePose, hmdOrigin, hmdAxis );
	//UpdateHmdOriginAndAxis( predictedDevicePose, predictedHmdOrigin, predictedHmdAxis );
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

void OpenVrSupport::GetHeadTracking( idVec3& headOrigin, idMat3& headAxis ) {
	headOrigin = hmdOrigin;
	headAxis = hmdAxis;
}

void OpenVrSupport::AdjustViewWithPredictedHeadPose( renderView_t& eyeView, const int eye ) {
	eyeView.vieworg += hmdOrigin * eyeView.viewaxis;
	eyeView.viewaxis = hmdAxis * eyeView.viewaxis;

	float halfEyeSeparationCentimeters = 0.5f * GetInterPupillaryDistance();
	float halfEyeSeparationWorldUnits = halfEyeSeparationCentimeters / 2.309f;  // 1.1 world units are 1 inch
	eyeView.vieworg -= eye * halfEyeSeparationWorldUnits * eyeView.viewaxis[1];
	eyeView.viewEyeBuffer = eye;
	eyeView.halfEyeDistance = halfEyeSeparationWorldUnits;
	eyeView.hmdOrigin = predictedHmdOrigin;
	eyeView.hmdAxis = predictedHmdAxis;
}

void OpenVrSupport::AdjustViewWithActualHeadPose( viewDef_t* viewDef ) {
	// revert the predicted HMD pose and redo calculations with actual pose
	renderView_t& eyeView = viewDef->renderView;
	int eye = eyeView.viewEyeBuffer;
	eyeView.vieworg += eye * eyeView.halfEyeDistance * eyeView.viewaxis[1];
	eyeView.viewaxis = eyeView.hmdAxis.Inverse() * eyeView.viewaxis;
	eyeView.vieworg -= eyeView.hmdOrigin * eyeView.viewaxis;

	eyeView.vieworg += hmdOrigin * eyeView.viewaxis;
	eyeView.viewaxis = hmdAxis * eyeView.viewaxis;
	eyeView.vieworg -= eye * eyeView.halfEyeDistance * eyeView.viewaxis[1];

	// we need to also adapt the model view matrix of all objects to be rendered
	R_SetViewMatrix( viewDef );
	for (viewEntity_t * vEntity = viewDef->viewEntitys; vEntity; vEntity = vEntity->next) {
		myGlMultMatrix( vEntity->modelMatrix, viewDef->worldSpace.modelViewMatrix, vEntity->modelViewMatrix );
	}
}

void OpenVrSupport::FrameEnd() {
	vr::VRCompositor()->PostPresentHandoff();
}


void OpenVrSupport::InitParameters() {
	vrSystem->GetProjectionRaw( vr::Eye_Left, &rawProjection[0][0], &rawProjection[0][1], &rawProjection[0][2], &rawProjection[0][3] );
	common->Printf( "OpenVR left eye raw projection - l: %.2f r: %.2f t: %.2f b: %.2f\n", rawProjection[0][0], rawProjection[0][1], rawProjection[0][2], rawProjection[0][3] );
	vrSystem->GetProjectionRaw( vr::Eye_Right, &rawProjection[1][0], &rawProjection[1][1], &rawProjection[1][2], &rawProjection[1][3] );
	common->Printf( "OpenVR right eye raw projection - l: %.2f r: %.2f t: %.2f b: %.2f\n", rawProjection[1][0], rawProjection[1][1], rawProjection[1][2], rawProjection[1][3] );
	float combinedTanHalfFovHoriz = max( max( rawProjection[0][0], rawProjection[0][1] ), max( rawProjection[1][0], rawProjection[1][1] ) );
	float combinedTanHalfFovVert = max( max( rawProjection[0][2], rawProjection[0][3] ), max( rawProjection[1][2], rawProjection[1][3] ) );

	fovX = RAD2DEG( 2 * atanf( combinedTanHalfFovHoriz ) );
	fovY = RAD2DEG( 2 * atanf( combinedTanHalfFovVert ) );
	aspect = combinedTanHalfFovHoriz / combinedTanHalfFovVert;
	common->Printf( "OpenVR field of view x: %.1f  y: %.1f  Aspect: %.2f\n", fovX, fovY, aspect );

	scale = 1 / 0.02309f; // meters to world units (1.1 inch)

	vr::HmdMatrix34_t eyeToHead = vrSystem->GetEyeToHeadTransform( vr::Eye_Right );
	eyeForward = -eyeToHead.m[2][3];
	common->Printf( "Distance from eye to head: %.2f m\n", eyeForward );

	hmdOrigin.Zero();
	hmdAxis.Identity();
	predictedHmdOrigin.Zero();
	predictedHmdAxis.Identity();
}
