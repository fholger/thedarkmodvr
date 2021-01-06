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
#include "precompiled.h"

#include "OpenXRInput.h"
#include "xr_loader.h"

OpenXRInput::OpenXRInput() {
	actionNames[XR_WALK] = "walk";
	actionNames[XR_SPRINT] = "sprint";
}

void OpenXRInput::Init( XrInstance instance, XrSession session ) {
	this->instance = instance;
	this->session = session;

	ingameActionSet = CreateActionSet( "gameplay" );
	CreateAction( ingameActionSet, XR_WALK, XR_ACTION_TYPE_VECTOR2F_INPUT );
	CreateAction( ingameActionSet, XR_SPRINT, XR_ACTION_TYPE_BOOLEAN_INPUT );
}

XrActionSet OpenXRInput::CreateActionSet( const idStr &name, uint32_t priority ) {
	XrActionSetCreateInfo createInfo { XR_TYPE_ACTION_SET_CREATE_INFO, nullptr };
	strncpy( createInfo.actionSetName, name.c_str(), XR_MAX_ACTION_SET_NAME_SIZE );
	strncpy( createInfo.localizedActionSetName, name.c_str(), XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE );
	createInfo.priority = priority;
	XrActionSet actionSet = nullptr;
	XrResult result = xrCreateActionSet( instance, &createInfo, &actionSet );
	XR_CheckResult( result, "creating action set", instance, false );
	return actionSet;
}


void OpenXRInput::CreateAction( XrActionSet actionSet, Action action, XrActionType actionType ) {
	XrActionCreateInfo createInfo { XR_TYPE_ACTION_CREATE_INFO, nullptr };
	strncpy( createInfo.actionName, actionNames[action].c_str(), XR_MAX_ACTION_NAME_SIZE );
	createInfo.actionType = actionType;
	createInfo.countSubactionPaths = 0;
	createInfo.subactionPaths = nullptr;
	strncpy( createInfo.localizedActionName, actionNames[action].c_str(), XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
	XrResult result = xrCreateAction( actionSet, &createInfo, &actions[action] );
	XR_CheckResult( result, "creating action", instance, false );
}
