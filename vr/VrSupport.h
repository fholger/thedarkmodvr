#pragma once
#include <cstdint>
#include "../renderer/RenderWorld.h"


namespace vr
{
	class IVRSystem;
}
struct viewDef_s;
class idImage;

const int LEFT_EYE = -1;
const int RIGHT_EYE = 1;

class VrSupport
{
public:
	virtual ~VrSupport() {}
	
	virtual void Init() = 0;
	virtual void Shutdown() = 0;

	virtual bool IsInitialized() const = 0;
	virtual void DetermineRenderTargetSize( uint32_t* width, uint32_t* height ) const = 0;
	virtual void FrameStart() = 0;
	virtual void GetFov( float& fovX, float& fovY ) = 0;
	virtual void AdjustViewWithCurrentHeadPose( renderView_t& eyeView ) = 0;
	virtual void AdjustViewWithActualHeadPose( viewDef_s* viewDef, int eye ) = 0;
	virtual void SetupProjectionMatrix( viewDef_s* viewDef ) = 0;
	virtual void FrameEnd( idImage* leftEyeImage, idImage* rightEyeImage ) = 0;
	virtual void EnableMenuOverlay( idImage* menuImage ) = 0;
	virtual void DisableMenuOverlay() = 0;
};

extern VrSupport* vrSupport;