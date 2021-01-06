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
#include <openxr/openxr.h>

class OpenXRInput {
public:
	OpenXRInput();
	
	void Init(XrInstance instance, XrSession session);
	void Destroy();

private:
	XrInstance instance = nullptr;
	XrSession session = nullptr;

	enum Action {
		XR_WALK = 0,
		XR_SPRINT,

		XR_NUM_ACTIONS
	};
	XrAction actions[XR_NUM_ACTIONS] = { nullptr };
	idStr actionNames[XR_NUM_ACTIONS];

	XrActionSet ingameActionSet = nullptr;

	XrActionSet CreateActionSet( const idStr &name, uint32_t priority = 0 );
	void CreateAction( XrActionSet actionSet, Action action, XrActionType actionType );
};

