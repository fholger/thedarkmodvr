#pragma once
class OpenVRSystem;

class OpenVRSupport
{
public:
	OpenVRSupport();
	~OpenVRSupport();
	void Init();
	void Shutdown();
private:
	std::auto_ptr<OpenVRSystem> vrSystem;
};

extern OpenVRSupport* vrSupport;