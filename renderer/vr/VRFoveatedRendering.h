/*****************************************************************************
                    The Dark Mod GPL Source Code

 This file is part of the The Dark Mod Source Code, originally based
 on the Doom 3 GPL Source Code as published in 2011.

 The Dark Mod Source Code is free software: you can redistribute it
 and/or modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation, either version 3 of the License,
 or (at your option) any later version. For details, see LICENSE.TXT.

 Project: The Dark Mod (http://www.thedarkmod.com/)

******************************************************************************/
#pragma once
#include "../FrameBuffer.h"

extern idCVar vr_useFixedFoveatedRendering;

class VRFoveatedRendering {
public:
	void Init();
	void Destroy();
	
	void PrepareVariableRateShading( int eye );
	void DisableVariableRateShading();
	void DrawRadialDensityMaskToDepth( int eye );
	void ReconstructImageFromRdm( int eye );
	
private:
	idImage *variableRateShadingImage[2] = { nullptr };
	idImage *rdmReconstructionImage = nullptr;
	FrameBuffer *rdmReconstructionFbo = nullptr;
	GLSLProgram *radialDensityMaskShader = nullptr;
	GLSLProgram *rdmReconstructShader = nullptr;
};
