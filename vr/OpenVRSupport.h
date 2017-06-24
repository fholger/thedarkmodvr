#pragma once

namespace vr
{
	class IVRSystem;
}


class OpenVRSupport
{
public:
	OpenVRSupport();
	~OpenVRSupport();
	void Init();
	void Shutdown();

	bool IsInitialized() const { return vrSystem != nullptr; }
private:
	vr::IVRSystem* vrSystem;
};

extern OpenVRSupport* vrSupport;