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
#include "Session_local.h"
#include "OpenXRBackend.h"
#include "xr_math.h"

idCVar vr_useMotionControllers( "vr_useMotionControllers", "0", CVAR_GAME|CVAR_BOOL|CVAR_ARCHIVE, "If enabled, use motion controllers for game input (wip)" );
idCVar vr_inputLefthanded( "vr_inputLefthanded", "0", CVAR_BOOL|CVAR_ARCHIVE|CVAR_GAME, "If enabled, assumes the dominant hand to be the left hand" );
idCVar vr_inputWalkHeadRelative( "vr_inputWalkHeadRelative", "0", CVAR_GAME|CVAR_BOOL|CVAR_RENDERER, "If enabled, movement is relative to head orientation. Otherwise, it is relative to controller orientation." );

namespace {
	struct actionName_t {
		OpenXRInput::Action action;
		const char *name;
		const char *description;
	};

	actionName_t actionNames[] = {
		{ OpenXRInput::XR_FORWARD, "forward", "Player walk forward axis" },
		{ OpenXRInput::XR_SIDE, "side", "Player walk side axis" },
		{ OpenXRInput::XR_MOVE_DIR_HAND, "move_dir_hand", "Player walk direction hand" },
		{ OpenXRInput::XR_YAW, "yaw", "Camera look yaw" },
		{ OpenXRInput::XR_PITCH, "pitch", "Camera look pitch" },
		{ OpenXRInput::XR_SPRINT, "sprint", "Player sprint" },
		{ OpenXRInput::XR_CROUCH, "crouch", "Player crouch" },
		{ OpenXRInput::XR_JUMP, "jump", "Player jump" },
		{ OpenXRInput::XR_FROB, "frob", "Player frob" },
		{ OpenXRInput::XR_ATTACK, "attack", "Attack" },
		{ OpenXRInput::XR_MENU_OPEN, "menu_open", "Open main menu" },
		{ OpenXRInput::XR_INVENTORY_OPEN, "inventory_open", "Open inventory menu" },
		{ OpenXRInput::XR_MENU_AIM, "menu_aim", "Menu pointer aim" },
		{ OpenXRInput::XR_MENU_CLICK, "menu_click", "Menu pointer click" },
		{ OpenXRInput::XR_AIM, "aim", "Frob aim controller pose" },
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

	xrStringToPath( instance, "/user/hand/left", &handPaths[0] );
	xrStringToPath( instance, "/user/hand/right", &handPaths[1] );

	XrReferenceSpaceCreateInfo spaceCreateInfo = {
		XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		nullptr,
		XR_REFERENCE_SPACE_TYPE_VIEW,
		{ {0, 0, 0, 1}, {0, 0, 0} },
	};
	XrResult result = xrCreateReferenceSpace( session, &spaceCreateInfo, &hmdSpace );
	XR_CheckResult( result, "creating HMD reference space", instance );

	CreateAllActions();
	LoadSuggestedBindings();
	AttachActionSets();

	activeMenuHand = !vr_inputLefthanded.GetBool();
}

void OpenXRInput::Destroy() {
	suggestedBindingProfiles.clear();
	actionSpaces.Clear();
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


void OpenXRInput::CreateAction( XrActionSet actionSet, Action action, XrActionType actionType, uint32_t numSubPaths, XrPath *subPaths ) {
	const actionName_t &actionName = *NameByAction( action );
	XrActionCreateInfo createInfo { XR_TYPE_ACTION_CREATE_INFO, nullptr };
	strncpy( createInfo.actionName, actionName.name, XR_MAX_ACTION_NAME_SIZE );
	createInfo.actionType = actionType;
	createInfo.countSubactionPaths = numSubPaths;
	createInfo.subactionPaths = subPaths;
	strncpy( createInfo.localizedActionName, actionName.description, XR_MAX_LOCALIZED_ACTION_NAME_SIZE );
	XrResult result = xrCreateAction( actionSet, &createInfo, &actions[action] );
	XR_CheckResult( result, "creating action", instance, false );

	if ( actionType == XR_ACTION_TYPE_POSE_INPUT ) {
		actionSpaces.AddGrow( {
			action, XR_NULL_PATH, CreateActionSpace( action, XR_NULL_PATH )
		} );
		for ( uint32_t i = 0; i < numSubPaths; ++i ) {
			actionSpaces.AddGrow( {
				action, subPaths[i], CreateActionSpace( action, subPaths[i] )
			} );
		}
	}
}

XrSpace OpenXRInput::CreateActionSpace( Action action, XrPath subPath ) {
	XrPosef origin;
	origin.position.x = origin.position.y = origin.position.z = 0;
	origin.orientation.x = origin.orientation.y = origin.orientation.z = 0;
	origin.orientation.w = 1;
	XrActionSpaceCreateInfo spaceInfo {
		XR_TYPE_ACTION_SPACE_CREATE_INFO,
		nullptr,
		actions[action],
		subPath,
		origin,
	};
	XrSpace space = nullptr;
	XrResult result = xrCreateActionSpace( session, &spaceInfo, &space );
	XR_CheckResult( result, "creating action space", instance, false );
	return space;	
}

void OpenXRInput::CreateAllActions() {
	ingameActionSet = CreateActionSet( "gameplay" );
	CreateAction( ingameActionSet, XR_FORWARD, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_SIDE, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_MOVE_DIR_HAND, XR_ACTION_TYPE_POSE_INPUT );
	CreateAction( ingameActionSet, XR_YAW, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_PITCH, XR_ACTION_TYPE_FLOAT_INPUT );
	CreateAction( ingameActionSet, XR_SPRINT, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_CROUCH, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_JUMP, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_FROB, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_ATTACK, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_MENU_OPEN, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_INVENTORY_OPEN, XR_ACTION_TYPE_BOOLEAN_INPUT );
	CreateAction( ingameActionSet, XR_AIM, XR_ACTION_TYPE_POSE_INPUT );

	menuActionSet = CreateActionSet( "menu" );
	CreateAction( menuActionSet, XR_MENU_AIM, XR_ACTION_TYPE_POSE_INPUT, 2, handPaths );
	CreateAction( menuActionSet, XR_MENU_CLICK, XR_ACTION_TYPE_BOOLEAN_INPUT, 2, handPaths );
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
	idList<XrActionSet> actionSets = { ingameActionSet, menuActionSet };
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
	if ( vr_inputLefthanded.GetBool() ) {
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

void OpenXRInput::UpdateInput( int axis[6], idList<padActionChange_t> &actionChanges, poseInput_t &poseInput, XrSpace referenceSpace, XrTime time ) {
	if ( !vr_useMotionControllers.GetBool() ) {
		return;
	}

	if ( sessLocal.guiActive || console->Active() || gameLocal.GetLocalPlayer()->ActiveGui() ) {
		HandleMenuInput( referenceSpace, time, actionChanges );
		return;
	}

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

	if ( vr_inputWalkHeadRelative.GetBool() ) {
		XrSpaceLocation location { XR_TYPE_SPACE_LOCATION, nullptr };
		result = xrLocateSpace( hmdSpace, referenceSpace, time, &location );
		XR_CheckResult( result, "getting HMD space", instance, false );
		poseInput.movementAxis = QuatFromXr( location.pose.orientation );
	} else {
		poseInput.movementAxis = QuatFromXr( GetPose( XR_MOVE_DIR_HAND, referenceSpace, time ).second.orientation );
	}

	auto frobHandState = GetPose( XR_AIM, referenceSpace, time );
	poseInput.frobHandAxis = QuatFromXr( frobHandState.second.orientation );
	poseInput.frobHandPos = Vec3FromXr( frobHandState.second.position );

	auto menuState = GetBool( XR_MENU_OPEN );
	if ( menuState.first ) {
		actionChanges.AddGrow( { UB_NONE, "escape", menuState.second } );
	}
	auto inventoryState = GetBool( XR_INVENTORY_OPEN );
	if ( inventoryState.first ) {
		actionChanges.AddGrow( { UB_INVENTORY_GRID, "", inventoryState.second } );
	}
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

	if (fakeAttackPressed) {
		common->Printf("Releasing attack\n");
		// fixme: adjust once we have actual attack handling
		actionChanges.AddGrow( { UB_ATTACK, "", false } );
		fakeAttackPressed = false;
	}

	auto attackState = GetBool(XR_ATTACK);
	if ( attackState.first ) {
		actionChanges.AddGrow( { UB_ATTACK, "", attackState.second } );
	}
}

void OpenXRInput::HandleMenuInput( XrSpace referenceSpace, XrTime time, idList<padActionChange_t> &actionChanges ) {
	idList<XrActiveActionSet> activeActionSets {
		{ menuActionSet, XR_NULL_PATH },
	};

	XrActionsSyncInfo syncInfo {
		XR_TYPE_ACTIONS_SYNC_INFO,
		nullptr,
		activeActionSets.Num(),
		activeActionSets.Ptr(),
	};
	XrResult result = xrSyncActions( session, &syncInfo );
	XR_CheckResult( result, "syncing actions", instance, false );

	auto handAimState = GetPose( XR_MENU_AIM, referenceSpace, time, handPaths[activeMenuHand] );
	idPlayer *player = gameLocal.GetLocalPlayer();
	idUserInterface* gui = sessLocal.guiActive;
	if ( !gui ) {
		gui = player->ActiveGui();
	}
	if ( handAimState.first && gui ) {
		idVec2 uiIntersect = FindGuiOverlayIntersection( handAimState.second );
		if ( ( uiIntersect - curOverlayIntersect ).LengthSqr() > 0.00001 ) {
			curOverlayIntersect = uiIntersect;
			sysEvent_t moveEvent = sys->GenerateMouseMoveEvent( 0, 0 );
			if ( gui == sessLocal.guiActive ) {
				gui->SetCursor( 640 * uiIntersect.x, 480 * uiIntersect.y );
				Sys_QueEvent( 0, moveEvent.evType, moveEvent.evValue, moveEvent.evValue2, moveEvent.evPtrLength, moveEvent.evPtr );
			} else {
				gui->SetCursor( 640 * uiIntersect.x, 480 * uiIntersect.y );
				auto command = gui->HandleEvent( &moveEvent, 0 );
				player->HandleGuiCommands( player, command );
			}
		}
	}

	auto menuClickState = GetBool( XR_MENU_CLICK, handPaths[activeMenuHand] );
	if ( menuClickState.first && gui ) {
		if ( gui == sessLocal.guiActive ) {
			sysEvent_t clickEvent = sys->GenerateMouseButtonEvent( 1, menuClickState.second );
			Sys_QueEvent( 0, clickEvent.evType, clickEvent.evValue, clickEvent.evValue2, clickEvent.evPtrLength, clickEvent.evPtr );
		} else {
			// hack: needed to pass the mission start dialogue
			actionChanges.AddGrow( { UB_ATTACK, "", menuClickState.second } );
			fakeAttackPressed = true;
		}
	}

	auto altHandClick = GetBool( XR_MENU_CLICK, handPaths[!activeMenuHand] );
	if ( altHandClick.first ) {
		activeMenuHand = !activeMenuHand;
	}
}

idVec2 OpenXRInput::FindGuiOverlayIntersection( XrPosef pointerPose ) {
	// FIXME: will need to be calculated dynamically in the future
	guiOverlayPose = { {0, 0, 0, 1}, {0, vr_uiOverlayVerticalOffset.GetFloat(), -vr_uiOverlayDistance.GetFloat()} };
	idQuat uiOrientation ( guiOverlayPose.orientation.x, guiOverlayPose.orientation.y, guiOverlayPose.orientation.z, guiOverlayPose.orientation.w );
	idVec3 uiOrigin ( guiOverlayPose.position.x, guiOverlayPose.position.y, guiOverlayPose.position.z );
	idQuat pointerOrientation ( pointerPose.orientation.x, pointerPose.orientation.y, pointerPose.orientation.z, pointerPose.orientation.w );
	idVec3 pointerOrigin ( pointerPose.position.x, pointerPose.position.y, pointerPose.position.z );

	idQuat pointerInUiOrientation = uiOrientation * pointerOrientation;
	idVec3 pointerInUiOrigin = uiOrientation * ( pointerOrigin - uiOrigin );
	idVec3 pointerDir = pointerInUiOrientation.ToRotation().GetVec();
	pointerDir.Normalize();

	float t = - pointerInUiOrigin.z / pointerDir.z;
	if ( t < 0 ) {
		return idVec2 (0, 0);
	}

	idVec2 intersection ( pointerInUiOrigin.y + t * pointerDir.y, pointerInUiOrigin.x + t * pointerDir.x );
	idVec2 overlaySize = 3 * idVec2( vr_uiOverlayAspect.GetFloat(), 1 ) * vr_uiOverlayHeight.GetFloat();
	idVec2 upperLeft = -.5f * overlaySize;
	intersection -= upperLeft;
	intersection /= overlaySize;
	return idVec2( 1 - intersection.x, 1 - intersection.y );
}

XrSpace OpenXRInput::FindActionSpace( Action action, XrPath subPath ) {
	for ( const auto &actionSpace : actionSpaces ) {
		if ( actionSpace.action == action && actionSpace.subPath == subPath ) {
			return actionSpace.space;
		}
	}
	return nullptr;
}

std::pair<bool, bool> OpenXRInput::GetBool( Action action, XrPath subPath ) {
	XrActionStateBoolean state { XR_TYPE_ACTION_STATE_BOOLEAN, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStateBoolean( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, state.currentState };
}

std::pair<bool, float> OpenXRInput::GetFloat( Action action, XrPath subPath ) {
	XrActionStateFloat state { XR_TYPE_ACTION_STATE_FLOAT, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStateFloat( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, state.currentState };
}

std::pair<bool, idVec2> OpenXRInput::GetVec2( Action action, XrPath subPath ) {
	XrActionStateVector2f state { XR_TYPE_ACTION_STATE_VECTOR2F, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStateVector2f( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );
	return { state.changedSinceLastSync, idVec2(state.currentState.x, state.currentState.y) };
}

std::pair<bool, XrPosef> OpenXRInput::GetPose( Action action, XrSpace referenceSpace, XrTime time, XrPath subPath ) {
	XrActionStatePose state { XR_TYPE_ACTION_STATE_POSE, nullptr };
	XrActionStateGetInfo info { XR_TYPE_ACTION_STATE_GET_INFO, nullptr, actions[action], subPath };
	XrResult result = xrGetActionStatePose( session, &info, &state );
	XR_CheckResult( result, "getting action state", instance, false );

	XrSpaceLocation location { XR_TYPE_SPACE_LOCATION, nullptr };
	result = xrLocateSpace( FindActionSpace( action, subPath ), referenceSpace, time, &location );
	XR_CheckResult( result, "getting action space", instance, false );
	return { state.isActive, location.pose };
}
