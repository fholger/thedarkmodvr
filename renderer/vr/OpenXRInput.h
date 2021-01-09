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
#include "GamepadInput.h"

class OpenXRInput {
public:
	void Init(XrInstance instance, XrSession session);
	void Destroy();

	void UpdateInput( int axis[6], idList<padActionChange_t> &actionChanges );

	enum Action {
		XR_FORWARD = 0,
		XR_SIDE,
		XR_YAW,
		XR_PITCH,
		XR_SPRINT,

		XR_NUM_ACTIONS,
		XR_INVALID = -1,
	};
private:
	XrInstance instance = nullptr;
	XrSession session = nullptr;

	XrAction actions[XR_NUM_ACTIONS] = { nullptr };

	XrActionSet ingameActionSet = nullptr;

	XrActionSet CreateActionSet( const idStr &name, uint32_t priority = 0 );
	void CreateAction( XrActionSet actionSet, Action action, XrActionType actionType );
	void CreateAllActions();
	void LoadSuggestedBindings();
	void RegisterSuggestedBindings();
	void AttachActionSets();

	std::pair<bool, bool> GetBool( Action action );
	std::pair<bool, float> GetFloat( Action action );
	std::pair<bool, idVec2> GetVec2( Action action );
};

