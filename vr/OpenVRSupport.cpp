#include "precompiled_engine.h"
#include "OpenVRSupport.h"
#include "OpenVRSystem.h"

OpenVRSupport vrLocal;
OpenVRSupport* vrSupport = &vrLocal;

OpenVRSupport::OpenVRSupport()
{
}

OpenVRSupport::~OpenVRSupport()
{
}

void OpenVRSupport::Init()
{
	std::auto_ptr<OpenVRSystem> initVrSystem(new OpenVRSystem);
	if (initVrSystem->Init())
	{
		vrSystem = initVrSystem;
	}
}

void OpenVRSupport::Shutdown()
{
	vrSystem.release();
}
