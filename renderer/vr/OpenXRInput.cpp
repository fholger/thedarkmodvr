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

namespace {
	struct actionName_t {
		OpenXRInput::Action action;
		const char *name;
		const char *description;
	};

	actionName_t actionNames[] = {
		{ OpenXRInput::XR_WALK, "walk", "Player walk" },
		{ OpenXRInput::XR_SPRINT, "sprint", "Player sprint" },
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

	ingameActionSet = CreateActionSet( "gameplay" );
	CreateAction( ingameActionSet, XR_WALK, XR_ACTION_TYPE_VECTOR2F_INPUT );
	CreateAction( ingameActionSet, XR_SPRINT, XR_ACTION_TYPE_BOOLEAN_INPUT );

	cmdSystem->AddCommand( "xr_profile", XR_Profile_f, CMD_FL_SYSTEM, "Creates a new XR input binding profile for a specific interaction profile path" );
	cmdSystem->AddCommand( "xr_bind", XR_Bind_f, CMD_FL_SYSTEM, "Binds an input action to the specified user path for a given XR profile" );

	if ( fileSystem->FindFile( userXrBindingsFile ) != FIND_NO ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, idStr::Fmt( "exec %s\n", userXrBindingsFile ) );
	} else if ( fileSystem->FindFile( defaultXrBindingsFile ) != FIND_NO ) {
		cmdSystem->BufferCommandText( CMD_EXEC_NOW, idStr::Fmt( "exec %s\n", defaultXrBindingsFile ) );
	}
	RegisterSuggestedBindings();
	
	cmdSystem->RemoveCommand( "xr_profile" );
	cmdSystem->RemoveCommand( "xr_bind" );
}

void OpenXRInput::Destroy() {
	suggestedBindingProfiles.clear();
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

void OpenXRInput::RegisterSuggestedBindings() {
	for ( const auto &it : suggestedBindingProfiles ) {
		const BindingProfile &profile = it.second;
		XrPath profilePath;
		XrResult result = xrStringToPath( instance, profile.profilePath, &profilePath );
		if ( XR_FAILED( result )) {
			common->Printf("Failed to convert to XR path: %s\n", profile.profilePath.c_str() );
			continue;
		}

		idList<XrActionSuggestedBinding> bindings;
		for ( SuggestedBinding binding : profile.bindings ) {
			XrPath bindingPath;
			result = xrStringToPath( instance, binding.binding, &bindingPath );
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
