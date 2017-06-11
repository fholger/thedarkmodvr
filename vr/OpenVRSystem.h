#pragma once

namespace vr
{
	class IVRSystem;
}

class OpenVRSystem
{
public:
	OpenVRSystem();
	~OpenVRSystem();
	bool Init();
	void Shutdown();
private:
	vr::IVRSystem* vrSystem;
};

