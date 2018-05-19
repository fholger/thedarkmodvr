#include "precompiled.h"
#include "VrSupport.h"
#include "openvr/openvr.h"
#include "../renderer/Image.h"
#include "../renderer/tr_local.h"
#include "Framebuffer.h"

idCVar vr_enable( "vr_enable", "1", CVAR_RENDERER | CVAR_BOOL, "enable OpenVR support" );
idCVar vr_renderScale( "vr_renderScale", "1", CVAR_RENDERER | CVAR_FLOAT, "set VR resolution scaling" );

class OpenVrSupport : public VrSupport {
public:
	OpenVrSupport();
	~OpenVrSupport() override;
	
	void Init() override;
	void Shutdown() override;
	bool IsInitialized() const override;
	void DetermineRenderTargetSize( uint32_t *width, uint32_t *height ) const override;
	void FrameStart() override;
	void GetFov( float& fovX, float& fovY ) override;
	void AdjustViewWithCurrentHeadPose( renderView_t& eyeView ) override;
	void AdjustViewWithActualHeadPose( viewDef_t* viewDef, int eye ) override;
	void SetupProjectionMatrix( viewDef_t* viewDef ) override;
	void FrameEnd( idImage *leftEyeImage, idImage *rightEyeImage ) override;
	void EnableMenuOverlay( idImage* menuImage ) override;
	void DisableMenuOverlay() override;
	void GetCurrentViewProjection( int eye, const renderView_t &eyeView, idVec3 &viewOrigin, float *viewMatrix, float *projectionMatrix ) override;
private:
	float GetInterPupillaryDistance() const;
	void SubmitEyeFrame( int eye, idImage* image );
	void UpdateHmdOriginAndAxis( const vr::TrackedDevicePose_t devicePose[16], idVec3& origin, idMat3& axis );
	void SetupProjectionMatrix(int viewEye, bool cramZNear, float* projectionMatrix);
	void SetupViewMatrix( const idVec3 & vieworg, const idMat3 & viewaxis, float * view_matrix );

	vr::IVRSystem *vrSystem;
	vr::VROverlayHandle_t menuOverlay;
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

extern Framebuffer * stereoEyeFBOs[2];
extern idImage * stereoEyeImages[2];


OpenVrSupport::OpenVrSupport(): vrSystem(nullptr) {
}

OpenVrSupport::~OpenVrSupport()
{
}

void OpenVrSupport::Init()
{
	if (!vr_enable.GetBool()) {
		return;
	}

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
		common->Warning("OpenVR initialization failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(initError));
		return;
	}

	vr::VRCompositor()->SetTrackingSpace( vr::TrackingUniverseSeated );
	vr::EVROverlayError overlayError = vr::VROverlay()->CreateOverlay( "tdm_menu_overlay", "The Dark Mod Menu", &menuOverlay );
	if (overlayError != vr::VROverlayError_None) {
		common->Warning( "OpenVR overlay initialization failed: %d", overlayError );
		Shutdown();
		return;
	}
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
		for (int i = 0; i < 2; ++i) {
			delete stereoEyeFBOs[i];
			delete stereoEyeImages[i];
		}
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
	*width *= vr_renderScale.GetFloat();
	*height *= vr_renderScale.GetFloat();
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
	} else {
		common->Warning( "OpenVR tracking pose is invalid" );
	}
}

void OpenVrSupport::FrameStart() {
	vr::VRCompositor()->WaitGetPoses( trackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0 );
	UpdateHmdOriginAndAxis( trackedDevicePose, hmdOrigin, hmdAxis );
}

void OpenVrSupport::SetupProjectionMatrix(viewDef_t* viewDef) {
	const float zNear = ( viewDef->renderView.cramZNear ) ? ( r_znear.GetFloat() * 0.25f ) : r_znear.GetFloat();
	// a "combined" projection matrix to cover both eyes' field of view
	// needed for the frontend to calculate correct scissor rectangles that are valid for both eyes
	const float idx = 1.0f / ( rawProjection[1][1] - rawProjection[0][0] );
	const float idy = 1.0f / ( rawProjection[1][3] - rawProjection[1][2] );
	const float sx = rawProjection[1][0] + rawProjection[0][1];
	const float sy = rawProjection[1][2] + rawProjection[1][3];

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

void OpenVrSupport::SetupProjectionMatrix( int viewEye, bool cramZNear, float *projectionMatrix ) {
	const float zNear = cramZNear ? (r_znear.GetFloat() * 0.25f) : r_znear.GetFloat();
	const int eye = viewEye == LEFT_EYE ? 0 : 1;
	const float idx = 1.0f / (rawProjection[eye][1] - rawProjection[eye][0]);
	const float idy = 1.0f / (rawProjection[eye][3] - rawProjection[eye][2]);
	const float sx = rawProjection[eye][0] + rawProjection[eye][1];
	const float sy = rawProjection[eye][2] + rawProjection[eye][3];

	projectionMatrix[0 * 4 + 0] = 2.0f * idx;
	projectionMatrix[1 * 4 + 0] = 0.0f;
	projectionMatrix[2 * 4 + 0] = sx * idx;
	projectionMatrix[3 * 4 + 0] = 0.0f;

	projectionMatrix[0 * 4 + 1] = 0.0f;
	projectionMatrix[1 * 4 + 1] = 2.0f * idy;
	projectionMatrix[2 * 4 + 1] = sy*idy;	
	projectionMatrix[3 * 4 + 1] = 0.0f;

	projectionMatrix[0 * 4 + 2] = 0.0f;
	projectionMatrix[1 * 4 + 2] = 0.0f;
	projectionMatrix[2 * 4 + 2] = -0.999f; 
	projectionMatrix[3 * 4 + 2] = -2.0f * zNear;

	projectionMatrix[0 * 4 + 3] = 0.0f;
	projectionMatrix[1 * 4 + 3] = 0.0f;
	projectionMatrix[2 * 4 + 3] = -1.0f;
	projectionMatrix[3 * 4 + 3] = 0.0f;
}

void OpenVrSupport::SetupViewMatrix( const idVec3 &origin, const idMat3 &viewaxis, float *viewMatrix ) {
	float	viewerMatrix[16];
	static float	s_flipMatrix[16] = {
		// convert from our coordinate system (looking down X)
		// to OpenGL's coordinate system (looking down -Z)
		0, 0, -1, 0,
		-1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 0, 1
	};

	viewerMatrix[0] = viewaxis[0][0];
	viewerMatrix[4] = viewaxis[0][1];
	viewerMatrix[8] = viewaxis[0][2];
	viewerMatrix[12] = -origin[0] * viewerMatrix[0] + -origin[1] * viewerMatrix[4] + -origin[2] * viewerMatrix[8];

	viewerMatrix[1] = viewaxis[1][0];
	viewerMatrix[5] = viewaxis[1][1];
	viewerMatrix[9] = viewaxis[1][2];
	viewerMatrix[13] = -origin[0] * viewerMatrix[1] + -origin[1] * viewerMatrix[5] + -origin[2] * viewerMatrix[9];

	viewerMatrix[2] = viewaxis[2][0];
	viewerMatrix[6] = viewaxis[2][1];
	viewerMatrix[10] = viewaxis[2][2];
	viewerMatrix[14] = -origin[0] * viewerMatrix[2] + -origin[1] * viewerMatrix[6] + -origin[2] * viewerMatrix[10];

	viewerMatrix[3] = 0;
	viewerMatrix[7] = 0;
	viewerMatrix[11] = 0;
	viewerMatrix[15] = 1;

	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	myGlMultMatrix( viewerMatrix, s_flipMatrix, viewMatrix );
}

void OpenVrSupport::GetFov( float& fovX, float& fovY ) {
	fovX = this->fovX;
	fovY = this->fovY;
}

void OpenVrSupport::AdjustViewWithCurrentHeadPose( renderView_t& eyeView ) {
	eyeView.vieworg += hmdOrigin * eyeView.viewaxis;
	eyeView.viewaxis = hmdAxis * eyeView.viewaxis;

	float halfEyeSeparationCentimeters = 0.5f * GetInterPupillaryDistance();
	float halfEyeSeparationWorldUnits = halfEyeSeparationCentimeters / 2.309f;  // 1.1 world units are 1 inch
	eyeView.viewEyeBuffer = -1;
	eyeView.halfEyeDistance = halfEyeSeparationWorldUnits;
	eyeView.hmdOrigin = hmdOrigin;
	eyeView.hmdAxis = hmdAxis;
}

void OpenVrSupport::AdjustViewWithActualHeadPose( viewDef_t* viewDef, int eye ) {
	// revert the predicted HMD pose and redo calculations with actual pose
	renderView_t& eyeView = viewDef->renderView;
	if( eye != RIGHT_EYE )
		eyeView.vieworg -= eye * eyeView.halfEyeDistance * eyeView.viewaxis[1];
	eyeView.viewaxis = eyeView.hmdAxis.Inverse() * eyeView.viewaxis;
	eyeView.vieworg -= eyeView.hmdOrigin * eyeView.viewaxis;

	eyeView.viewEyeBuffer = eye;
	eyeView.vieworg += hmdOrigin * eyeView.viewaxis;
	eyeView.viewaxis = hmdAxis * eyeView.viewaxis;
	eyeView.vieworg -= eye * eyeView.halfEyeDistance * eyeView.viewaxis[1];
	eyeView.hmdAxis = hmdAxis;
	eyeView.hmdOrigin = hmdOrigin;

	// we need to also adapt the model view matrix of all objects to be rendered
	SetupProjectionMatrix( viewDef );
	R_SetViewMatrix( *viewDef );
	for (viewEntity_t * vEntity = viewDef->viewEntitys; vEntity; vEntity = vEntity->next) {
		myGlMultMatrix( vEntity->modelMatrix, viewDef->worldSpace.modelViewMatrix, vEntity->modelViewMatrix );
	}
}

void OpenVrSupport::FrameEnd( idImage* leftEyeImage, idImage* rightEyeImage ) {
	SubmitEyeFrame( LEFT_EYE, leftEyeImage );
	SubmitEyeFrame( RIGHT_EYE, rightEyeImage );
	//vr::VRCompositor()->PostPresentHandoff();
}

void OpenVrSupport::EnableMenuOverlay( idImage* menuImage ) {
	vr::Texture_t texture = { (void*)menuImage->texnum, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
	vr::VROverlay()->SetOverlayTexture( menuOverlay, &texture );
	vr::VROverlay()->SetHighQualityOverlay( menuOverlay );
	vr::HmdMatrix34_t transform;
	memset( transform.m, 0, sizeof( transform.m ) );
	transform.m[0][0] = 1;
	transform.m[1][1] = -1;
	transform.m[2][2] = -1;
	transform.m[2][3] = -2;
	vr::VROverlay()->SetOverlayWidthInMeters( menuOverlay, 4 );
	vr::VROverlay()->SetOverlayTransformAbsolute( menuOverlay, vr::TrackingUniverseSeated, &transform );
	vr::VROverlay()->ShowOverlay( menuOverlay );
}

void OpenVrSupport::DisableMenuOverlay() {
	vr::VROverlay()->HideOverlay( menuOverlay );
	vr::VROverlay()->ClearOverlayTexture( menuOverlay );
}

void OpenVrSupport::GetCurrentViewProjection( int eye, const renderView_t &eyeView, idVec3 &viewOrigin, float *viewMatrix, float *projectionMatrix ) {
	// Replace HMD prediction from last frame with the current prediction for a more accurate result
	idMat3 viewAxis = eyeView.hmdAxis.Inverse() * eyeView.viewaxis;
	viewOrigin = eyeView.vieworg - (eyeView.hmdOrigin * viewAxis);

	viewOrigin += hmdOrigin * viewAxis;
	viewAxis = hmdAxis * viewAxis;
	viewOrigin -= eye * eyeView.halfEyeDistance * viewAxis[1];

	SetupProjectionMatrix( eye, eyeView.cramZNear, projectionMatrix );
	SetupViewMatrix( viewOrigin, eyeView.viewaxis, viewMatrix );
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
