#pragma once

namespace vr
{
	class IVRSystem;
}

const int LEFT_EYE = -1;
const int RIGHT_EYE = 1;

class OpenVRSupport
{
public:
	virtual ~OpenVRSupport() {}
	
	virtual void Init() = 0;
	virtual void Shutdown() = 0;

	virtual bool IsInitialized() const = 0;
	virtual float GetInterPupillaryDistance() const = 0;
	virtual void DetermineRenderTargetSize( uint32_t* width, uint32_t* height ) const = 0;
	virtual void SubmitEyeFrame( int eye, idImage* image ) = 0;
	virtual void FrameStart() = 0;
};

extern OpenVRSupport* vrSupport;