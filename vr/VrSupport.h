#pragma once
#include "../tools/compilers/dmap/dmap.h"

namespace vr
{
	class IVRSystem;
}
struct viewDef_s;

const int LEFT_EYE = -1;
const int RIGHT_EYE = 1;

class VrSupport
{
public:
	virtual ~VrSupport() {}
	
	virtual void Init() = 0;
	virtual void Shutdown() = 0;

	virtual bool IsInitialized() const = 0;
	virtual float GetInterPupillaryDistance() const = 0;
	virtual void DetermineRenderTargetSize( uint32_t* width, uint32_t* height ) const = 0;
	virtual void SubmitEyeFrame( int eye, idImage* image ) = 0;
	virtual void FrameStart() = 0;
	virtual void SetupProjectionMatrix( viewDef_s* viewDef ) = 0;
	virtual void GetFov( float& fovX, float& fovY ) = 0;
	virtual void GetHeadTracking( idVec3& headOrigin, idMat3& headAxis ) = 0;
	virtual void AdjustViewWithPredictedHeadPose( renderView_t& eyeView, const int eye ) = 0;
	virtual void AdjustViewWithActualHeadPose( viewDef_t* viewDef ) = 0;
	virtual void FrameEnd() = 0;
};

extern VrSupport* vrSupport;