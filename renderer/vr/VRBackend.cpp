#include "precompiled.h"
#include "VRBackend.h"
#include "OpenXRBackend.h"

VRBackend *vr = nullptr;

idCVar vr_backend( "vr_backend", "0", CVAR_RENDERER|CVAR_INTEGER|CVAR_ARCHIVE, "0 - OpenVR, 1 - OpenXR" );

void SelectVRImplementation() {
	vr = xrBackend;	
}
