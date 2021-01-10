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
#include <map>

idCVar vr_lefthanded( "vr_lefthanded", "0", CVAR_BOOL|CVAR_ARCHIVE|CVAR_GAME, "If enabled, assumes the dominant hand to be the left hand" );

namespace {
	struct actionName_t {
		OpenXRInput::Action action;
		const char *name;
		const char *description;
	};

	actionName_t actionNames[] = {
		{ OpenXRInput::XR_FORWARD, "forward", "Player walk forward axis" },
		{ OpenXRInput::XR_SIDE, "side", "Player walk side axis" },
		{ OpenXRInput::XR_YAW, "yaw", "Camera look yaw" },
		{ OpenXRInput::XR_PITCH, "pitch", "Camera look pitch" },
		{ OpenXRInput::XR_SPRINT, "sprint", "Player sprint" },
		{ OpenXRInput::XR_CROUCH, "crouch", "Player crouch" },
		{ OpenXRInput::XR_JUMP, "jump", "Player jump" },
		{ OpenXRInput::XR_FROB, "frob", "Player frob" },
	};

	OpenXRInput::Action ActionByName( const idStr &name ) {
		for ( auto actionName : actionNames ) {
			if ( idStr::Icmp( name, actionName.name ) == 0 ) {
				return actionName.action;
			}
		}
		return OpenXRInput::XR_INVALID;
	}

	actionName_t *NameByAction( OpenXRInput::Action action ) {
		for ( auto &actionName : actionNames ) {
			if ( actionName.action == action ) {
				return &actionName;
			}
		}
		return nullptr;
	}

	struct SuggestedBinding {
		OpenXRInput::Action action;
		idStr binding;
	};

	struct BindingProfile {
		idStr profilePath;		
		idList<SuggestedBinding> bindings;
	};

	std::map<idStr, BindingProfile> suggestedBindingProfiles;

	void XR_Profile_f( const idCmdArgs &args ) {
		int numArgs = args.Argc();
		if ( numArgs != 3 ) {
			common->Printf( "xr_profile <name> <xr_interaction_profile_path>\n" );
			return;
		}

		suggestedBindingProfiles[args.Argv(1)].profilePath = args.Argv(2);
	}

	void XR_Bind_f( const idCmdArgs &args ) {
		int numArgs = args.Argc();
		if ( numArgs != 4 ) {
			common->Printf( "xr_bind <profile> <action> <xr_binding_path>\n" );
		}

		idStr profile = args.Argv( 1 );
		if ( suggestedBindingProfiles.find( profile ) == suggestedBindingProfiles.end() ) {
			common->Printf( "No such profile: %s\n", profile.c_str() );
			return;
		}
		OpenXRInput::Action action = ActionByName( args.Argv( 2 ) );
		if ( action == OpenXRInput::XR_INVALID ) {
			common->Printf( "Invalid action name: %s\n", args.Argv(2) );
			return;
		}
		SuggestedBinding binding {
			action,
			args.Argv( 3 ),
		};
		suggestedBindingProfiles[profile].bindings.AddGrow( binding );
	}

	const char *defaultXrBindingsFile = "XrProfileBindings.default.cfg";
	const char *userXrBindingsFile = "XrProfileBindings.user.cfg";
}

void OpenXRInput::Init( XrInstance instance, XrSession session ) {
	this->instance = instance;
	this->session = session;

	CreateAllActions();
	LoadSuggestedBindings();
	AttachActionSets();
}

void OpenXRInput::Destroy() {
	suggestedBindingProfiles.clear();
}

void OpenXRInput::UpdateInput( int axis[6], idList<padActionChange_t> &actionChanges ) {
	idList<XrActiveActionSet> activeActionSets {
		{ ingameActionSet, XR_NULL_PATH },
	};

	XrActionsSyncInfo syncInfo {
		XR_TYPE_ACTIONS_SYNC_INFO,
		nullptr,
		activeActionSets.Num(),
		activeActionSets.Ptr(),
	};
	XrResult result = xrSyncActions( session, &syncInfo );
	XR_CheckResult( result, "syncing actions", instance, false );

	float forward = GetFloat( XR_FORWARD ).second;
	float side = GetFloat( XR_SIDE ).second;
	float yaw = GetFloat( XR_YAW ).second;
	float pitch = GetFloat( XR_PITCH ).second;
	axis[AXIS_SIDE] = 127.f * side;
	axis[AXIS_FORWARD] = 127.f * forward;
	axis[AXIS_YAW] = 127.f * yaw;
	axis[AXIS_PITCH] = 127.f * pitch;

	auto jumpState = GetBool( XR_JUMP );
	if ( jumpState.first ) {
		actionChanges.AddGrow( { UB_UP, "", jumpState.second } );
	}
	auto frobState = GetBool( XR_FROB );
	if ( frobState.first ) {
		actionChanges.AddGrow( { UB_FROB, "", frobState.second } );
	}
	auto crouchState = GetBool( XR_CROUCH );
	if ( crouchState.first ) {
		actionChanges.AddGrow( { UB_CROUCH, "", crouchState.second } );
	}
	if ( isSprinting && idMath::Fabs( side ) + idMath::Fabs( forward ) <= 0.001 ) {
		actionChanges.AddGrow( { UB_SPEED, "", false } );
		isSprinting = false;
	}
	auto sprintState = GetBool( XR_SPRINT );
	if ( sprintState.first && sprintState.second && !isSprinting ) {
		actionChanges.AddGrow( { UB_SPEED, "", true } );
		isSprinting = true;
	}
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
	const actionName_t &actionName = *NameByAction( action );
	XrActionCreateInfo createInfo { XR_TYPE_ACTION_CREATE_INFO, nullptr };
	strncpy( createInfo.actionName, actionName.name, XR_MAX_ACTION_NAME_SIZE );
	createInfo.actionType = actionType;
	createInfo.countSubactionPaths = 0;
	createInfo.subactionPaths = nullptr;
	strncpy( createInfo.localizedActionName, actionName.description, XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
	XrResult result = xrCreateAction( actionSet, &createInfo, &actions[action] );
	XR_CheckResult( result, "creating action", instance, false );
}

void OpenXRInput::CreateAllActions() {
	ingameActionSet = CreateActionSet( "gameplay" );
	CreateAction( ingameActionSet, XR_FORWARD, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_SIDE, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_YAW, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_PITCH, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_SPRINT, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_CROUCH, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_JUMP, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_FROB, XR_ACTION_TYPE_BOOLEAN_INPUT );
}

void OpenXRInput::LoadSuggestedBindings() {
	cmdSystem->AddCommand( "xr_profile", XR_Profile_f, CMD_FL_SYSTEM, "Creates a new XR input binding profile for a specific interaction profile path" );
	cmdSystem->AddCommand( "xr_bind", XR_Bind_f, CMD_FL_SYSTEM, "Binds an input action to the specified user path for a given XR profile" );

	if ( fileSystem->FindFile( userXrBindingsFile ) != FIND_NO ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, idStr::Fmt( "exec %s\n", userXrBindingsFile ) );
	} else if ( fileSystem->FindFile( defaultXrBindingsFile ) != FIND_NO ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, idStr::Fmt( "exec %s\n", defaultXrBindingsFile ) );
	}
	cmdSystem->ExecuteCommandBuffer();
	RegisterSuggestedBindings();
	
	//cmdSystem->RemoveCommand( "xr_profile" );
	//cmdSystem->RemoveCommand( "xr_bind" );
}

void OpenXRInput::RegisterSuggestedBindings() {
	for ( const auto &it : suggestedBindingProfiles ) {
		const BindingProfile &profile = it.second;
		common->Printf( "Suggesting bindings for %s ...\n", profile.profilePath.c_str() );
		XrPath profilePath;
		XrResult result = xrStringToPath( instance, profile.profilePath, &profilePath );
		if ( XR_FAILED( result )) {
			common->Printf("Failed to convert to XR path: %s\n", profile.profilePath.c_str() );
			continue;
		}

		idList<XrActionSuggestedBinding> bindings;
		for ( SuggestedBinding binding : profile.bindings ) {
			XrPath bindingPath;
			result = xrStringToPath( instance, ApplyDominantHandToActionPath( profile.profilePath, binding.binding ), &bindingPath );
			if ( XR_FAILED( result ) ) {
				common->Printf( "Failed to convert to XR path: %s\n", binding.binding.c_str() );
				continue;
			}
			bindings.AddGrow( {
				actions[binding.action],
				bindingPath,
			} );
		}

		XrInteractionProfileSuggestedBinding suggestedBindings {
			XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
			nullptr,
			profilePath,
			bindings.Num(),
			bindings.Ptr(),
		};
		result = xrSuggestInteractionProfileBindings( instance, &suggestedBindings );
		XR_CheckResult( result, "suggesting input bindings", instance, false );
	}
}

void OpenXRInput::AttachActionSets() {
	idList<XrActionSet> actionSets = { ingameActionSet };
	XrSessionActionSetsAttachInfo attachInfo {
		XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
		nullptr,
		actionSets.Num(),
		actionSets.Ptr(),
	};
	XrResult result = xrAttachSessionActionSets( session, &attachInfo );
	XR_CheckResult( result, "attaching action sets", instance, false );
}

idStr OpenXRInput::ApplyDominantHandToActionPath( const idStr &profile, const idStr &path ) {
	idStr result = path;
	if ( vr_lefthanded.GetBool() ) {
		if ( profile == "/interaction_profiles/hp/mixed_reality_controller" || profile == "/interaction_profiles/oculus/touch_controller" ) {
			result.Replace( "/user/hand/main/input/a/", "/user/hand/left/input/x/" );
			result.Replace( "/user/hand/main/input/b/", "/user/hand/left/input/y/" );
		}
		result.Replace( "/user/hand/main/", "/user/hand/left/" );
		result.Replace( "/user/hand/off/", "/user/hand/right/" );
	} else {
		if ( profile == "/interaction_profiles/hp/mixed_reality_controller" || profile == "/interaction_profiles/oculus/touch_controller" ) {
			result.Replace( "/user/hand/off/input/a/", "/user/hand/left/input/x/" );
			result.Replace( "/user/hand/off/input/b/", "/user/hand/left/input/y/" );
		}
		result.Replace( "/user/hand/main/", "/user/hand/right/" );
		result.Replace( "/user/hand/off/", "/user/hand/left/" );
	}
	return result;
}

std::pair<bool, bool> OpenXRInput::GetBool( Action action ) {
	XrActionStateBoolean state { XR_TYPE_ACTION_STATE_BOOLEAN, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], XR_NULL_PATH };
	XrResult result = xrGetActionStateBoolean( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, state.currentState };
}

std::pair<bool, float> OpenXRInput::GetFloat( Action action ) {
	XrActionStateFloat state { XR_TYPE_ACTION_STATE_FLOAT, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], XR_NULL_PATH };
	XrResult result = xrGetActionStateFloat( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, state.currentState };
}

std::pair<bool, idVec2> OpenXRInput::GetVec2( Action action ) {
	XrActionStateVector2f state { XR_TYPE_ACTION_STATE_VECTOR2F, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], XR_NULL_PATH };
	XrResult result = xrGetActionStateVector2f( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, idVec2(state.currentState.x, state.currentState.y) };
}
