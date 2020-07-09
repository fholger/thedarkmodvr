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

class VRBackend {
public:
	virtual void Init() = 0;
	virtual void Destroy() = 0;

	virtual void BeginFrame() = 0;
	virtual void SubmitFrame() = 0;
 
	virtual void AdjustRenderView( renderView_t *view ) = 0;
	virtual void RenderStereoView( const emptyCommand_t * cmds ) = 0;
};

extern VRBackend *vrBackend;
extern idCVar vr_uiResolution;

void SelectVRImplementation();
