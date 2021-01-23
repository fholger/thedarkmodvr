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
#pragma hdrstop



#include "tr_local.h"
#include "math/Line.h"

static idCVarInt r_useAreaLocks( "r_useAreaLocks", "3", CVAR_RENDERER, "1 - suppress multiple entity/area refs, 2 - lights, 3 - both" );

/*
===================
R_ListRenderLightDefs_f
===================
*/
void R_ListRenderLightDefs_f( const idCmdArgs &args ) {
	int			i;
	idRenderLightLocal	*ldef;

	if ( !tr.primaryWorld ) {
		return;
	}
	int active = 0;
	int	totalRef = 0;
	int	totalIntr = 0;

	for ( i = 0 ; i < tr.primaryWorld->lightDefs.Num() ; i++ ) {
		ldef = tr.primaryWorld->lightDefs[i];
		if ( !ldef ) {
			common->Printf( "%4i: FREED\n", i );
			continue;
		}

		// count up the interactions
		int	iCount = 0;
		for ( idInteraction *inter = ldef->firstInteraction; inter != NULL; inter = inter->lightNext ) {
			iCount++;
		}
		totalIntr += iCount;

		// count up the references
		int	rCount = 0;
		for ( areaReference_t *ref = ldef->references ; ref ; ref = ref->ownerNext ) {
			rCount++;
		}
		totalRef += rCount;

		common->Printf( "%4i: %3i intr %2i refs %s\n", i, iCount, rCount, ldef->lightShader->GetName());
		active++;
	}

	common->Printf( "%i lightDefs, %i interactions, %i areaRefs\n", active, totalIntr, totalRef );
}

/*
===================
R_ListRenderEntityDefs_f
===================
*/
void R_ListRenderEntityDefs_f( const idCmdArgs &args ) {
	int			i;
	idRenderEntityLocal	*mdef;

	if ( !tr.primaryWorld ) {
		return;
	}
	int active = 0;
	int	totalRef = 0;
	int	totalIntr = 0;

	for ( i = 0 ; i < tr.primaryWorld->entityDefs.Num() ; i++ ) {
		mdef = tr.primaryWorld->entityDefs[i];
		if ( !mdef ) {
			common->Printf( "%4i: FREED\n", i );
			continue;
		}

		// count up the interactions
		int	iCount = 0;
		for ( idInteraction *inter = mdef->firstInteraction; inter != NULL; inter = inter->entityNext ) {
			iCount++;
		}
		totalIntr += iCount;

		// count up the references
		int	rCount = 0;
		for ( areaReference_t *ref = mdef->entityRefs ; ref ; ref = ref->ownerNext ) {
			rCount++;
		}
		totalRef += rCount;

		common->Printf( "%4i: %3i intr %2i refs %s\n", i, iCount, rCount, mdef->parms.hModel->Name());
		active++;
	}

	common->Printf( "total active: %i\n", active );
}

/*
===================
idRenderWorldLocal::idRenderWorldLocal
===================
*/
idRenderWorldLocal::idRenderWorldLocal() {
	mapName.Clear();
	mapTimeStamp = FILE_NOT_FOUND_TIMESTAMP;

	generateAllInteractionsCalled = false;

	areaNodes = NULL;
	numAreaNodes = 0;

	//portalAreas = NULL;
	//numPortalAreas = 0;

	//doublePortals = NULL;
	//numInterAreaPortals = 0;

	interactionTable.Init();
}

/*
===================
idRenderWorldLocal::~idRenderWorldLocal
===================
*/
idRenderWorldLocal::~idRenderWorldLocal() {
	// free all the entityDefs, lightDefs, portals, etc
	FreeWorld();

	// free up the debug lines, polys, and text
	R_ClearDebugPolygons( 0 );
	R_ClearDebugLines( 0 );
	R_ClearDebugText( 0 );
}

/*
===================
AddEntityDef
===================
*/
qhandle_t idRenderWorldLocal::AddEntityDef( const renderEntity_t *re ){
	// try and reuse a free spot
	int entityHandle = entityDefs.FindNull();
	if ( entityHandle == -1 ) 
		entityHandle = entityDefs.Append( NULL );

	UpdateEntityDef( entityHandle, re );
	
	return entityHandle;
}

/*
==============
UpdateEntityDef

Does not write to the demo file, which will only be updated for
visible entities
==============
*/
int c_callbackUpdate;

void idRenderWorldLocal::UpdateEntityDef( qhandle_t entityHandle, const renderEntity_t *re ) {
	if ( r_skipUpdates.GetBool() ) {
		return;
	}

	tr.pc.c_entityUpdates++;

	if ( !re->hModel && !re->callback ) {
		common->Error( "idRenderWorld::UpdateEntityDef: NULL hModel" );
	}

	// create new slots if needed
	if ( entityHandle < 0 || entityHandle > LUDICROUS_INDEX ) {
		common->Error( "idRenderWorld::UpdateEntityDef: index = %i", entityHandle );
	}
	while ( entityHandle >= entityDefs.Num() ) {
		entityDefs.Append( NULL );
	}

	idRenderEntityLocal	*def = entityDefs[entityHandle];
	if ( def ) {

		if ( !re->forceUpdate ) {

			// check for exact match (OPTIMIZE: check through pointers more)
			if ( !re->joints && !re->callbackData && !def->dynamicModel && !memcmp( re, &def->parms, sizeof( *re ) ) ) {
				return;
			}

			// if the only thing that changed was shaderparms, we can just leave things as they are
			// after updating parms

			// if we have a callback function and the bounds, origin, axis and model match,
			// then we can leave the references as they are
			if ( re->callback ) {

				bool axisMatch = ( re->axis == def->parms.axis );
				bool originMatch = ( re->origin == def->parms.origin );
				bool boundsMatch = ( re->bounds == def->referenceBounds );
				bool modelMatch = ( re->hModel == def->parms.hModel );

				if ( boundsMatch && originMatch && axisMatch && modelMatch ) {
					// only clear the dynamic model and interaction surfaces if they exist
					c_callbackUpdate++;
					R_ClearEntityDefDynamicModel( def );
					def->parms = *re;
					return;
				}
			}
		}

		// save any decals if the model is the same, allowing marks to move with entities
		if ( def->parms.hModel == re->hModel ) {
			R_FreeEntityDefDerivedData( def, true, true );
		} else {
			R_FreeEntityDefDerivedData( def, false, false );
		}
	} else {
		// creating a new one
		def = new idRenderEntityLocal;
		entityDefs[entityHandle] = def;

		def->world = this;
		def->index = entityHandle;
	}

	def->parms = *re;

	R_AxisToModelMatrix( def->parms.axis, def->parms.origin, def->modelMatrix );

	def->lastModifiedFrameNum = tr.frameCount;
	if ( session->writeDemo && def->archived ) {
		WriteFreeEntity( entityHandle );
		def->archived = false;
	}

	// optionally immediately issue any callbacks
	if ( !r_useEntityCallbacks.GetBool() && def->parms.callback ) {
		R_IssueEntityDefCallback( def );
	}

	// based on the model bounds, add references in each area
	// that may contain the updated surface
	R_CreateEntityRefs( def );
}

/*
===================
FreeEntityDef

Frees all references and lit surfaces from the model, and
NULL's out it's entry in the world list
===================
*/
void idRenderWorldLocal::FreeEntityDef( qhandle_t entityHandle ) {
	idRenderEntityLocal	*def;

	if ( entityHandle < 0 || entityHandle >= entityDefs.Num() ) {
		common->Printf( "idRenderWorld::FreeEntityDef: handle %i > %i\n", entityHandle, entityDefs.Num() );
		return;
	}

	def = entityDefs[entityHandle];
	if ( !def ) {
		common->Printf( "idRenderWorld::FreeEntityDef: handle %i is NULL\n", entityHandle );
		return;
	}

	R_FreeEntityDefDerivedData( def, false, false );

	if ( session->writeDemo && def->archived ) {
		WriteFreeEntity( entityHandle );
	}

	// if we are playing a demo, these will have been freed
	// in R_FreeEntityDefDerivedData(), otherwise the gui
	// object still exists in the game

	def->parms.gui[ 0 ] = NULL;
	def->parms.gui[ 1 ] = NULL;
	def->parms.gui[ 2 ] = NULL;

	delete def;
	entityDefs[ entityHandle ] = NULL;
}

/*
==================
GetRenderEntity
==================
*/
const renderEntity_t *idRenderWorldLocal::GetRenderEntity( qhandle_t entityHandle ) const {
	idRenderEntityLocal	*def;

	if ( entityHandle < 0 || entityHandle >= entityDefs.Num() ) {
		common->Printf( "idRenderWorld::GetRenderEntity: invalid handle %i [0, %i]\n", entityHandle, entityDefs.Num() );
		return NULL;
	}

	def = entityDefs[entityHandle];
	if ( !def ) {
		common->Printf( "idRenderWorld::GetRenderEntity: handle %i is NULL\n", entityHandle );
		return NULL;
	}

	return &def->parms;
}

/*
==================
AddLightDef
==================
*/
qhandle_t idRenderWorldLocal::AddLightDef( const renderLight_t *rlight ) {
	// try and reuse a free spot
	int lightHandle = lightDefs.FindNull();

	if ( lightHandle == -1 ) 
		lightHandle = lightDefs.Append( NULL );
	UpdateLightDef( lightHandle, rlight );

	return lightHandle;
}

/*
=================
UpdateLightDef

The generation of all the derived interaction data will
usually be deferred until it is visible in a scene

Does not write to the demo file, which will only be done for visible lights
=================
*/
void idRenderWorldLocal::UpdateLightDef( qhandle_t lightHandle, const renderLight_t *rlight ) {
	if ( r_skipUpdates.GetBool() ) {
		return;
	}

	tr.pc.c_lightUpdates++;

	// create new slots if needed
	if ( lightHandle < 0 || lightHandle > LUDICROUS_INDEX ) {
		common->Error( "idRenderWorld::UpdateLightDef: index = %i", lightHandle );
	}
	while ( lightHandle >= lightDefs.Num() ) {
		lightDefs.Append( NULL );
	}

	bool justUpdate = false;
	idRenderLightLocal *light = lightDefs[lightHandle];
	if ( light ) {
		// if the shape of the light stays the same, we don't need to dump
		// any of our derived data, because shader parms are calculated every frame
		if ( rlight->axis == light->parms.axis && rlight->end == light->parms.end &&
			 rlight->lightCenter == light->parms.lightCenter && rlight->lightRadius == light->parms.lightRadius &&
			 rlight->noShadows == light->parms.noShadows && rlight->origin == light->parms.origin &&
			 rlight->parallel == light->parms.parallel && rlight->pointLight == light->parms.pointLight &&
			 rlight->right == light->parms.right && rlight->start == light->parms.start &&
			 rlight->target == light->parms.target && rlight->up == light->parms.up && 
			 rlight->shader == light->lightShader && rlight->prelightModel == light->parms.prelightModel ) {
			justUpdate = true;
		} else {
			// if we are updating shadows, the prelight model is no longer valid
			light->lightHasMoved = true;
			R_FreeLightDefDerivedData( light );
		}
	} else {
		// create a new one
		light = new idRenderLightLocal;
		lightDefs[lightHandle] = light;

		light->world = this;
		light->index = lightHandle;
	}

	light->parms = *rlight;
	light->lastModifiedFrameNum = tr.frameCount;
	if ( session->writeDemo && light->archived ) {
		WriteFreeLight( lightHandle );
		light->archived = false;
	}

	if ( light->lightHasMoved ) {
		light->parms.prelightModel = NULL;
	}

	if (!justUpdate) {
		R_DeriveLightData( light );
		R_CreateLightRefs( light );
		R_CreateLightDefFogPortals( light );
	}
}

/*
====================
FreeLightDef

Frees all references and lit surfaces from the light, and
NULL's out it's entry in the world list
====================
*/
void idRenderWorldLocal::FreeLightDef( qhandle_t lightHandle ) {
	idRenderLightLocal	*light;

	if ( lightHandle < 0 || lightHandle >= lightDefs.Num() ) {
		common->Printf( "idRenderWorld::FreeLightDef: invalid handle %i [0, %i]\n", lightHandle, lightDefs.Num() );
		return;
	}

	light = lightDefs[lightHandle];
	if ( !light ) {
		common->Printf( "idRenderWorld::FreeLightDef: handle %i is NULL\n", lightHandle );
		return;
	}

	R_FreeLightDefDerivedData( light );

	if ( session->writeDemo && light->archived ) {
		WriteFreeLight( lightHandle );
	}

	delete light;
	lightDefs[lightHandle] = NULL;
}

/*
==================
GetRenderLight
==================
*/
const renderLight_t *idRenderWorldLocal::GetRenderLight( qhandle_t lightHandle ) const {
	idRenderLightLocal *def;

	if ( lightHandle < 0 || lightHandle >= lightDefs.Num() ) {
		common->Printf( "idRenderWorld::GetRenderLight: handle %i > %i\n", lightHandle, lightDefs.Num() );
		return NULL;
	}

	def = lightDefs[lightHandle];
	if ( !def ) {
		common->Printf( "idRenderWorld::GetRenderLight: handle %i is NULL\n", lightHandle );
		return NULL;
	}

	return &def->parms;
}

/*
================
idRenderWorldLocal::ProjectDecalOntoWorld
================
*/
void idRenderWorldLocal::ProjectDecalOntoWorld( const idFixedWinding &winding, const idVec3 &projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial *material, const int startTime ) {
	int i, areas[10], numAreas;
	const areaReference_t *ref;
	const portalArea_t *area;
	const idRenderModel *model;
	idRenderEntityLocal *def;
	decalProjectionInfo_t info, localInfo;

	if ( !idRenderModelDecal::CreateProjectionInfo( info, winding, projectionOrigin, parallel, fadeDepth, material, startTime ) ) {
		return;
	}

	// get the world areas touched by the projection volume
	numAreas = BoundsInAreas( info.projectionBounds, areas, 10 );

	// check all areas for models
	for ( i = 0; i < numAreas; i++ ) {

		area = &portalAreas[ areas[i] ];

		// check all models in this area
		for ( ref = area->entityRefs.areaNext; ref != &area->entityRefs; ref = ref->areaNext ) {
			def = ref->entity;

			// completely ignore any dynamic or callback models
			model = def->parms.hModel;
			if ( model == NULL || model->IsDynamicModel() != DM_STATIC || def->parms.callback ) {
				continue;
			}

			if ( def->parms.customShader != NULL && !def->parms.customShader->AllowOverlays() ) {
				continue;
			}

			idBounds bounds;
			bounds.FromTransformedBounds( model->Bounds( &def->parms ), def->parms.origin, def->parms.axis );

			// if the model bounds do not overlap with the projection bounds
			if ( !info.projectionBounds.IntersectsBounds( bounds ) ) {
				continue;
			}

			// transform the bounding planes, fade planes and texture axis into local space
			idRenderModelDecal::GlobalProjectionInfoToLocal( localInfo, info, def->parms.origin, def->parms.axis );
			localInfo.force = ( def->parms.customShader != NULL );

			if ( !def->decals ) {
				def->decals = idRenderModelDecal::Alloc();
			}
			def->decals->CreateDecal( model, localInfo );
		}
	}
}

/*
====================
idRenderWorldLocal::ProjectDecal
====================
*/
void idRenderWorldLocal::ProjectDecal( qhandle_t entityHandle, const idFixedWinding &winding, const idVec3 &projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial *material, const int startTime ) {
	decalProjectionInfo_t info, localInfo;

	if ( entityHandle < 0 || entityHandle >= entityDefs.Num() ) {
		common->Error( "idRenderWorld::ProjectOverlay: index = %i", entityHandle );
		return;
	}

	idRenderEntityLocal	*def = entityDefs[ entityHandle ];
	if ( !def ) {
		return;
	}

	const idRenderModel *model = def->parms.hModel;

	if ( model == NULL || model->IsDynamicModel() != DM_STATIC || def->parms.callback ) {
		return;
	}

	if ( !idRenderModelDecal::CreateProjectionInfo( info, winding, projectionOrigin, parallel, fadeDepth, material, startTime ) ) {
		return;
	}

	idBounds bounds;
	bounds.FromTransformedBounds( model->Bounds( &def->parms ), def->parms.origin, def->parms.axis );

	// if the model bounds do not overlap with the projection bounds
	if ( !info.projectionBounds.IntersectsBounds( bounds ) ) {
		return;
	}

	// transform the bounding planes, fade planes and texture axis into local space
	idRenderModelDecal::GlobalProjectionInfoToLocal( localInfo, info, def->parms.origin, def->parms.axis );
	localInfo.force = ( def->parms.customShader != NULL );

	if ( def->decals == NULL ) {
		def->decals = idRenderModelDecal::Alloc();
	}
	def->decals->CreateDecal( model, localInfo );
}

/*
====================
idRenderWorldLocal::ProjectOverlay
====================
*/
void idRenderWorldLocal::ProjectOverlay( qhandle_t entityHandle, const idPlane localTextureAxis[2], const idMaterial *material ) {

	if ( entityHandle < 0 || entityHandle >= entityDefs.Num() ) {
		common->Error( "idRenderWorld::ProjectOverlay: index = %i", entityHandle );
		return;
	}

	idRenderEntityLocal	*def = entityDefs[ entityHandle ];
	if ( !def ) {
		return;
	}

	const renderEntity_t *refEnt = &def->parms;

	idRenderModel *model = refEnt->hModel;
	if ( model->IsDynamicModel() != DM_CACHED ) {	// FIXME: probably should be MD5 only
		return;
	}
	model = R_EntityDefDynamicModel( def );

	if ( !def->overlay ) {
		def->overlay = idRenderModelOverlay::Alloc();
	}

	def->overlay->CreateOverlay( model, localTextureAxis, material, refEnt->customSkin, refEnt->customShader ); // Skin params added -- SteveL #3844
}

/*
====================
idRenderWorldLocal::RemoveDecals
====================
*/
void idRenderWorldLocal::RemoveDecals( qhandle_t entityHandle ) {
	if ( entityHandle < 0 || entityHandle >= entityDefs.Num() ) {
		common->Error( "idRenderWorld::ProjectOverlay: index = %i", entityHandle );
		return;
	}

	idRenderEntityLocal	*def = entityDefs[ entityHandle ];
	if ( !def ) {
		return;
	}

	R_FreeEntityDefDecals( def );
	R_FreeEntityDefOverlay( def );
}

/*
====================
SetRenderView

Sets the current view so any calls to the render world will use the correct parms.
====================
*/
void idRenderWorldLocal::SetRenderView( const renderView_t *renderView ) {
	tr.primaryRenderView = *renderView;
}

/*
====================
RenderScene

Draw a 3D view into a part of the window, then return
to 2D drawing.

Rendering a scene may require multiple views to be rendered
to handle mirrors,
====================
*/
void idRenderWorldLocal::RenderScene( const renderView_t &renderView ) {
#ifndef	ID_DEDICATED
	//renderView_t	copy;

	if ( !glConfig.isInitialized ) {
		return;
	}

	//copy = *renderView;

	// skip front end rendering work, which will result
	// in only gui drawing
	if ( r_skipFrontEnd.GetBool() ) {
		return;
	}

	if ( renderView.fov_x <= 0 || renderView.fov_y <= 0 ) {
		common->Error( "idRenderWorld::RenderScene: bad FOVs: %f, %f", renderView.fov_x, renderView.fov_y );
	}

	// close any gui drawing
	/*tr.guiModel->EmitFullScreen();
	tr.guiModel->Clear();*/

	int startTime = Sys_Milliseconds();

	// setup view parms for the initial view
	//
	viewDef_t		*parms = (viewDef_t *)R_ClearedFrameAlloc( sizeof( *parms ) );
	parms->renderView = renderView;

	if ( tr.takingScreenshot ) {
		parms->renderView.forceUpdate = true;
	}

	// set up viewport, adjusted for resolution and OpenGL style 0 at the bottom
	tr.RenderViewToViewport( parms->renderView, parms->viewport );

	// the scissor bounds may be shrunk in subviews even if
	// the viewport stays the same
	// this scissor range is local inside the viewport
	parms->scissor.x1 = 0;
	parms->scissor.y1 = 0;
	parms->scissor.x2 = parms->viewport.x2 - parms->viewport.x1;
	parms->scissor.y2 = parms->viewport.y2 - parms->viewport.y1;


	parms->isSubview = false;
	parms->initialViewAreaOrigin = renderView.vieworg;
	parms->floatTime = parms->renderView.time * 0.001f;
	parms->renderWorld = this;

	// use this time for any subsequent 2D rendering, so damage blobs/etc 
	// can use level time
	tr.frameShaderTime = parms->floatTime;

	// see if the view needs to reverse the culling sense in mirrors
	// or environment cube sides
	idVec3	cross;
	cross = parms->renderView.viewaxis[1].Cross( parms->renderView.viewaxis[2] );
	if ( cross * parms->renderView.viewaxis[0] > 0 ) {
		parms->isMirror = false;
	} else {
		parms->isMirror = true;
	}

	if ( r_lockSurfaces.GetBool() ) {
		R_LockSurfaceScene( *parms );
		return;
	}

	// save this world for use by some console commands
	tr.primaryWorld = this;
	tr.primaryRenderView = renderView;
	tr.primaryView = parms;

	// rendering this view may cause other views to be rendered
	// for mirrors / portals / shadows / environment maps
	// this will also cause any necessary entities and lights to be
	// updated to the demo file
	R_RenderView( *parms );

	// now write delete commands for any modified-but-not-visible entities, and
	// add the renderView command to the demo
	if ( session->writeDemo ) {
		WriteRenderView( renderView );
	}

#if 0
	for ( int i = 0 ; i < entityDefs.Num() ; i++ ) {
		idRenderEntityLocal	*def = entityDefs[i];
		if ( !def ) {
			continue;
		}
		if ( def->parms.callback ) {
			continue;
		}
		if ( def->parms.hModel->IsDynamicModel() == DM_CONTINUOUS ) {
		}
	}
#endif

	int endTime = Sys_Milliseconds();

	tr.pc.frontEndMsec += endTime - startTime;

	// prepare for any 2D drawing after this
	//tr.guiModel->Clear();
#endif
}

/*
===================
NumAreas
===================
*/
int idRenderWorldLocal::NumAreas( void ) const {
	return portalAreas.Num();
}

/*
===================
NumPortalsInArea
===================
*/
int idRenderWorldLocal::NumPortalsInArea( int areaNum ) {
	//int				count;
	//portal_t		*portal;

	if ( areaNum >= portalAreas.Num() || areaNum < 0 ) {
		common->Error( "idRenderWorld::NumPortalsInArea: bad areanum %i", areaNum );
	}
	auto& area = portalAreas[areaNum];
	return (int)area.areaPortals.Num();
	/*count = 0;
	for ( portal = area->portals ; portal ; portal = portal->next ) {
		count++;
	}
	return count;*/
}

/*
===================
GetPortal
===================
*/
exitPortal_t idRenderWorldLocal::GetPortal( int areaNum, int portalNum ) {
	//int				count;
	if ( areaNum > portalAreas.Num() ) {
		common->Error( "idRenderWorld::GetPortal: areaNum > numAreas" );
	}
	auto& area = portalAreas[areaNum];

	//count = 0;
	//for ( int i = 0; i < area.areaPortals.size(); i++ ) {
		auto portal = area.areaPortals[portalNum];
		//if ( count == portalNum ) {
			exitPortal_t ret( portal->w );
			ret.areas[0] = areaNum;
			ret.areas[1] = portal->intoArea;
			//ret.w = portal->w;
			ret.blockingBits = portal->doublePortal->blockingBits;
			ret.lossPlayer = portal->doublePortal->lossPlayer; // grayman #3042
			//ret.portalHandle = portal->doublePortal - doublePortals + 1;
			ret.portalHandle = portal->doublePortal - &doublePortals[0] + 1;
			return ret;
		//}
		//count++;
	//}

	common->Error( "idRenderWorld::GetPortal: portalNum > numPortals" );

	memset( &ret, 0, sizeof( ret ) );
	return ret;
}

/*
==============
SetPortalPlayerLoss
==============
*/
void idRenderWorldLocal::SetPortalPlayerLoss( qhandle_t portal, float loss ) // grayman #3042
{
	if ( portal == 0 )
	{
		return;
	}

	if ( ( portal < 1 ) || ( portal > doublePortals.Num() ) )
	{
		common->Error( "SetPortalPlayerLoss: bad portal number %i", portal );
	}
	doublePortals[portal-1].lossPlayer = loss; // grayman #3042

	if ( session->writeDemo )
	{
		session->writeDemo->WriteInt( DS_RENDER );
		session->writeDemo->WriteInt( DC_SET_PLAYER_PORTAL_LOSS );
		session->writeDemo->WriteInt( portal );
		session->writeDemo->WriteFloat( loss );
	}
}

/*
===============
PointInAreaNum

Will return -1 if the point is not in an area, otherwise
it will return 0 <= value < tr.world->numPortalAreas
===============
*/
int idRenderWorldLocal::PointInArea( const idVec3 &point ) const {
	areaNode_t	*node;
	int			nodeNum;
	float		d;
	
	node = areaNodes;
	if ( !node ) {
		return -1;
	}
	while( 1 ) {
		d = point * node->plane.Normal() + node->plane[3];
		if (d > 0) {
			nodeNum = node->children[0];
		} else {
			nodeNum = node->children[1];
		}
		if ( nodeNum == 0 ) {
			return -1;		// in solid
		}
		if ( nodeNum < 0 ) {
			nodeNum = -1 - nodeNum;
			if ( nodeNum >= portalAreas.Num() ) {
				common->Error( "idRenderWorld::PointInArea: area out of range" );
			}
			return nodeNum;
		}
		node = areaNodes + nodeNum;
	}
	
	return -1;
}

/*
===================
BoundsInAreas_r
===================
*/
void idRenderWorldLocal::BoundsInAreas_r( int nodeNum, const idBounds &bounds, int *areas, int *numAreas, int maxAreas ) const {
	int side, i;
	areaNode_t *node;

	do {
		if ( nodeNum < 0 ) {
			nodeNum = -1 - nodeNum;

			for ( i = 0; i < (*numAreas); i++ ) {
				if ( areas[i] == nodeNum ) {
					break;
				}
			}
			if ( i >= (*numAreas) && (*numAreas) < maxAreas ) {
				areas[(*numAreas)++] = nodeNum;
			}
			return;
		}

		node = areaNodes + nodeNum;

		side = bounds.PlaneSide( node->plane );
		if ( side == PLANESIDE_FRONT ) {
			nodeNum = node->children[0];
		}
		else if ( side == PLANESIDE_BACK ) {
			nodeNum = node->children[1];
		}
		else {
			if ( node->children[1] != 0 ) {
				BoundsInAreas_r( node->children[1], bounds, areas, numAreas, maxAreas );
				if ( (*numAreas) >= maxAreas ) {
					return;
				}
			}
			nodeNum = node->children[0];
		}
	} while( nodeNum != 0 );

	return;
}

/*
===================
BoundsInAreas

  fills the *areas array with the number of the areas the bounds are in
  returns the total number of areas the bounds are in
===================
*/
int idRenderWorldLocal::BoundsInAreas( const idBounds &bounds, int *areas, int maxAreas ) const {
	int numAreas = 0;

	assert( areas );
	assert( bounds[0][0] <= bounds[1][0] && bounds[0][1] <= bounds[1][1] && bounds[0][2] <= bounds[1][2] );
	static const float sizeCap = 3e+5;
	assert( bounds[1][0] - bounds[0][0] < sizeCap && bounds[1][1] - bounds[0][1] < sizeCap && bounds[1][2] - bounds[0][2] < sizeCap );

	if ( !areaNodes ) {
		return numAreas;
	}
	BoundsInAreas_r( 0, bounds, areas, &numAreas, maxAreas );
	return numAreas;
}

#if 0
/*
================
GuiTrace

checks a ray trace against any gui surfaces in an entity, returning the
fraction location of the trace on the gui surface, or -1,-1 if no hit.
this doesn't do any occlusion testing, simply ignoring non-gui surfaces.
start / end are in global world coordinates.
================
*/
guiPoint_t	idRenderWorldLocal::GuiTrace( qhandle_t entityHandle, const idVec3 start, const idVec3 end ) const {
	localTrace_t	local;
	idVec3			localStart, localEnd, bestPoint;
	int				j;
	idRenderModel	*model;
	srfTriangles_t	*tri;
	const idMaterial *shader;
	guiPoint_t	pt;

	pt.x = pt.y = -1;
	pt.guiId = 0;

	if ( ( entityHandle < 0 ) || ( entityHandle >= entityDefs.Num() ) ) {
		common->Printf( "idRenderWorld::GuiTrace: invalid handle %i\n", entityHandle );
		return pt;
	}

	idRenderEntityLocal *def = entityDefs[entityHandle];	
	if ( !def ) {
		common->Printf( "idRenderWorld::GuiTrace: handle %i is NULL\n", entityHandle );
		return pt;
	}

	model = def->parms.hModel;
	if ( def->parms.callback || !def->parms.hModel || def->parms.hModel->IsDynamicModel() != DM_STATIC ) {
		return pt;
	}

	// transform the points into local space
	R_GlobalPointToLocal( def->modelMatrix, start, localStart );
	R_GlobalPointToLocal( def->modelMatrix, end, localEnd );

	for ( j = 0 ; j < model->NumSurfaces() ; j++ ) {
		const modelSurface_t *surf = model->Surface( j );

		tri = surf->geometry;
		if ( !tri ) {
			continue;
		}

		shader = R_RemapShaderBySkin( surf->shader, def->parms.customSkin, def->parms.customShader );
		if ( !shader ) {
			continue;
		}
		// only trace against gui surfaces
		if (!shader->HasGui()) {
			continue;
		}

		local = R_LocalTrace( localStart, localEnd, 0.0f, tri );
		if ( local.fraction < 1.0 ) {
			idVec3				origin, axis[3];
			idVec3				cursor;
			float				axisLen[2];

			R_SurfaceToTextureAxis( tri, origin, axis );
			cursor = local.point - origin;

			axisLen[0] = axis[0].Length();
			axisLen[1] = axis[1].Length();

			pt.x = ( cursor * axis[0] ) / ( axisLen[0] * axisLen[0] );
			pt.y = ( cursor * axis[1] ) / ( axisLen[1] * axisLen[1] );
			pt.guiId = shader->GetEntityGui();

			return pt;
		}
	}

	return pt;
}
#endif

/*
===================
idRenderWorldLocal::ModelTrace
===================
*/
bool idRenderWorldLocal::ModelTrace( modelTrace_t &trace, qhandle_t entityHandle, const idVec3 &start, const idVec3 &end, const float radius ) const {
	int i;
	bool collisionSurface;
	const modelSurface_t *surf;
	localTrace_t localTrace;
	idRenderModel *model;
	float modelMatrix[16];
	idVec3 localStart, localEnd;
	const idMaterial *shader;

	trace.fraction = 1.0f;

	if ( entityHandle < 0 || entityHandle >= entityDefs.Num() ) {
//		common->Error( "idRenderWorld::ModelTrace: index = %i", entityHandle );
		return false;
	}

	idRenderEntityLocal	*def = entityDefs[entityHandle];
	if ( !def ) {
		return false;
	}

	renderEntity_t *refEnt = &def->parms;

	model = R_EntityDefDynamicModel( def );
	if ( !model ) {
		return false;
	}

	// transform the points into local space
	R_AxisToModelMatrix( refEnt->axis, refEnt->origin, modelMatrix );
	R_GlobalPointToLocal( modelMatrix, start, localStart );
	R_GlobalPointToLocal( modelMatrix, end, localEnd );

	// if we have explicit collision surfaces, only collide against them
	// (FIXME, should probably have a parm to control this)
	collisionSurface = false;
	for ( i = 0; i < model->NumBaseSurfaces(); i++ ) {
		surf = model->Surface( i );

		shader = R_RemapShaderBySkin( surf->material, def->parms.customSkin, def->parms.customShader );

		if ( shader->GetSurfaceFlags() & SURF_COLLISION ) {
			collisionSurface = true;
			break;
		}
	}

	// only use baseSurfaces, not any overlays
	for ( i = 0; i < model->NumBaseSurfaces(); i++ ) {
		surf = model->Surface( i );

		shader = R_RemapShaderBySkin( surf->material, def->parms.customSkin, def->parms.customShader );

		if ( !surf->geometry || !shader ) {
			continue;
		}

		if ( collisionSurface ) {
			// only trace vs collision surfaces
			if ( !( shader->GetSurfaceFlags() & SURF_COLLISION ) ) {
				continue;
			}
		} else {
			// skip if not drawn or translucent
			if ( !shader->IsDrawn() || ( shader->Coverage() != MC_OPAQUE && shader->Coverage() != MC_PERFORATED ) ) {
				continue;
			}
		}

		localTrace = R_LocalTrace( localStart, localEnd, radius, surf->geometry );

		if ( localTrace.fraction < trace.fraction ) {
			trace.fraction = localTrace.fraction;
			R_LocalPointToGlobal( modelMatrix, localTrace.point, trace.point );
			trace.normal = localTrace.normal * refEnt->axis;
			trace.material = shader;
			trace.entity = &def->parms;
			trace.model = model;
			trace.surfIdx = i;
			trace.jointNumber = refEnt->hModel->NearestJoint( i, localTrace.indexes[0], localTrace.indexes[1], localTrace.indexes[2] );
		}
	}

	return ( trace.fraction < 1.0f );
}

bool idRenderWorldLocal::TraceAll( modelTrace_t &trace, const idVec3 &start, const idVec3 &end, bool fastWorld, float radius, TraceFilterFunc filterCallback, void *context ) const {
	memset( &trace, 0, sizeof( modelTrace_t ) );
	trace.fraction = 1.0f;
	trace.point = end;
	idVec3 radiusVec(radius, radius, radius);

	// stgatilov: this is almost the same algorithm that I put into idClip

	// bounds for the whole trace
	idBounds traceBounds;
	traceBounds.Clear();
	traceBounds.AddPoint( start );
	traceBounds.AddPoint( end );
	idVec3 invDirection = GetInverseMovementVelocity( start, end );

	int areas[4096];		// I have seen max 750 visportals on map
	int numAreas = 0;

	// collide vs whole world using BSP tree optimization
	// also get the world areas the trace is in
	if ( fastWorld ) {
		//FastWorldTrace( trace, start, end );
		// note: it is faster to collide with BSP and enumerate areas in one pass:
		if ( areaNodes != NULL ) {
			RecurseProcBSP_r( &trace, areas, &numAreas, 4096, -1, 0, 0.0f, 1.0f, start, end );
		}
	}
	else {
		numAreas = BoundsInAreas( traceBounds, areas, 4096 );
	}
	if (numAreas >= 4096)
		common->Warning("idRenderWorldLocal::TraceAll: areas array overflow");

	struct Candidate {
		idRenderEntityLocal *def;
		const idRenderModel *model;
		int surfIdx;
		const idMaterial *material;
		float fractionLower;
	};
	idList<Candidate> candidates;

	// check all areas for models
	for ( int a = 0; a < numAreas; a++ ) {
		const portalArea_t &area = portalAreas[areas[a]];

		// check all models in this area
		for ( const areaReference_t *ref = area.entityRefs.areaNext; ref != &area.entityRefs; ref = ref->areaNext ) {
			idRenderEntityLocal *def = ref->entity;

			const idRenderModel *model = def->parms.hModel;
			if ( !model )
				continue;

			// note: the very first reference must be for "_areaN" model, i.e. world geometry of the area
			if ( ref == area.entityRefs.areaNext )
				assert( model->IsStaticWorldModel() );
			if ( model->IsStaticWorldModel() ) {
				idBounds areaBounds = model->Bounds();
				if ( !areaBounds.LineIntersection( start, trace.point ) ) {
					// does not intersect area => does not intersect any entities inside it
					break;
				}
			}

			// filter 1: by entity
			if ( filterCallback && !filterCallback(context, &def->parms, nullptr, nullptr) )
				continue;

			model = R_EntityDefDynamicModel( def );
			if ( !model )
				continue;

			// filter 2: by entity & model
			if ( filterCallback && !filterCallback(context, &def->parms, model, nullptr) )
				continue;

			idBounds entityBounds;
			entityBounds.FromTransformedBounds( model->Bounds( &def->parms ), def->parms.origin, def->parms.axis );
			// if the model bounds do not overlap with the trace bounds
			if ( !traceBounds.IntersectsBounds( entityBounds ) || !entityBounds.LineIntersection( start, trace.point )  )
				continue;

			for ( int s = 0; s < model->NumSurfaces(); s++ ) {
				const modelSurface_t *surf = model->Surface( s );

				const idMaterial *material = R_RemapShaderBySkin( surf->material, def->parms.customSkin, def->parms.customShader );

				// if no geometry or no shader
				if ( !surf->geometry || !material )
					continue;

				if ( fastWorld && model->IsStaticWorldModel() && (material->Coverage() == MC_OPAQUE) )
					continue;	// already intersected

				idBounds surfBounds;
				surfBounds.FromTransformedBounds( surf->geometry->bounds, def->parms.origin, def->parms.axis );
				float interval[2] = { 0.0f, trace.fraction };
				// intersect surface bounds with trace line
				if ( !MovingBoundsIntersectBounds( start, invDirection, radiusVec, surfBounds, interval ) )
					continue;	// no intersection

				// filter 3: by entity & model & material
				if ( filterCallback && !filterCallback( context, &def->parms, model, material ) )
					continue;

				bool duplicate = false;
				for ( int j = 0; j < candidates.Num(); j++ )
					if ( candidates[j].def == def && candidates[j].surfIdx == s ) {
						duplicate = true;
						break;
					}
				// avoid tracing multi-area entity many times
				if ( duplicate )
					continue;

				Candidate c = {def, model, s, material, interval[0]};
				candidates.Append(c);
			}
		}
	}

	// be greedy: sort by min fraction possible (as deduced from bounds)
	std::sort( candidates.begin(), candidates.end(), []( const Candidate &a, const Candidate &b) -> bool {
		return a.fractionLower < b.fractionLower;
	} );

	for ( int c = 0; c < candidates.Num(); c++ ) {
		// all the rest candidates are surely even farther along line
		if ( candidates[c].fractionLower >= trace.fraction ) {
			break;
		}
		idRenderEntityLocal *def = candidates[c].def;
		const idRenderModel *model = candidates[c].model;
		int surfIdx = candidates[c].surfIdx;
		const modelSurface_t *surf = model->Surface( surfIdx );
		const idMaterial *material = candidates[c].material;

		const srfTriangles_t *tri = surf->geometry;

		// transform the points into local space
		float modelMatrix[16];
		idVec3 localStart, localEnd;
		R_AxisToModelMatrix( def->parms.axis, def->parms.origin, modelMatrix );
		R_GlobalPointToLocal( modelMatrix, start, localStart );
		R_GlobalPointToLocal( modelMatrix, end, localEnd );

		localTrace_t localTrace = R_LocalTrace( localStart, localEnd, radius, surf->geometry );

		if ( localTrace.fraction < trace.fraction ) {
			trace.fraction = localTrace.fraction;
			R_LocalPointToGlobal( modelMatrix, localTrace.point, trace.point );
			trace.normal = localTrace.normal * def->parms.axis;
			trace.material = material;
			trace.entity = &def->parms;
			trace.model = model;
			trace.surfIdx = surfIdx;
			trace.jointNumber = model->NearestJoint( surfIdx, localTrace.indexes[0], localTrace.indexes[1], localTrace.indexes[2] );

			traceBounds.Clear();
			traceBounds.AddPoint( start );
			traceBounds.AddPoint( start + trace.fraction * (end - start) );
		}
	}

	return trace.fraction < 1.0f;
}

/*
===================
idRenderWorldLocal::Trace
===================
*/
// FIXME: _D3XP added those.
const char* playerModelExcludeList[] = {
	"models/md5/chars/thief/thief_head.md5mesh",
	"models/md5/chars/thief/tdm_ai_thief.md5mesh",
	NULL
};

const char* playerMaterialExcludeList[] = {
	"models/props/misc/lightgem_surface",
	NULL
};

bool idRenderWorldLocal::Trace( modelTrace_t &trace, const idVec3 &start, const idVec3 &end, const float radius, bool skipDynamic, bool skipPlayer /*_D3XP*/ ) const {
	auto filter = [&](const renderEntity_t *renderEntity, const idRenderModel *renderModel, const idMaterial *material) -> bool {
		if (material) {
			/* _D3XP addition. */
			if ( skipPlayer ) {
				const char *name = material->GetName();
				for ( int k = 0; playerMaterialExcludeList[k]; k++ ) {
					const char *exclude = playerMaterialExcludeList[k];
					if ( strcmp(name, exclude) == 0 )
						return false;
				}
			}
		}
		else if ( renderEntity ) {
			idRenderModel *model = renderEntity->hModel;
			if ( model->IsDynamicModel() != DM_STATIC ) {
				if ( skipDynamic )
					return false;
				/* _D3XP addition. */
				if ( skipPlayer ) {
					const char *name = model->Name();
					for ( int k = 0; playerModelExcludeList[k]; k++ ) {
						const char *exclude = playerModelExcludeList[k];
						if ( strcmp(name, exclude) == 0 )
							return false;
					}
				}
			}
		}
		return true;
	};
	return TraceAll(trace, start, end, false, radius, LambdaToFuncPtr(filter), &filter);
}

/*
==================
idRenderWorldLocal::RecurseProcBSP
==================
*/
void idRenderWorldLocal::RecurseProcBSP_r( modelTrace_t *results, int *areas, int *numAreas, int maxAreas, int parentNodeNum, int nodeNum, float p1f, float p2f, const idVec3 &p1, const idVec3 &p2 ) const {
	float		t1, t2;
	float		frac;
	idVec3		mid;
	int			side;
	float		midf;
	areaNode_t *node;

	if ( results->fraction <= p1f) {
		return;		// already hit something nearer
	}
	// empty leaf
	if ( nodeNum < 0 ) {
		if ( maxAreas == 0 )
			return;
		nodeNum = -1 - nodeNum;
		int i;
		for ( i = 0; i < *numAreas; i++ ) {
			if ( areas[i] == nodeNum )
				break;
		}
		if ( i >= *numAreas && *numAreas < maxAreas ) {
			areas[(*numAreas)++] = nodeNum;
		}
		return;
	}
	// if solid leaf node
	if ( nodeNum == 0 ) {
		if ( parentNodeNum != -1 ) {

			results->fraction = p1f;
			results->point = p1;
			node = &areaNodes[parentNodeNum];
			results->normal = node->plane.Normal();
			return;
		}
	}
	node = &areaNodes[nodeNum];

	// distance from plane for trace start and end
	t1 = node->plane.Normal() * p1 + node->plane[3];
	t2 = node->plane.Normal() * p2 + node->plane[3];

	if ( t1 >= 0.0f && t2 >= 0.0f ) {
		RecurseProcBSP_r( results, areas, numAreas, maxAreas, nodeNum, node->children[0], p1f, p2f, p1, p2 );
		return;
	}
	if ( t1 < 0.0f && t2 < 0.0f ) {
		RecurseProcBSP_r( results, areas, numAreas, maxAreas, nodeNum, node->children[1], p1f, p2f, p1, p2 );
		return;
	}
	side = t1 < t2;
	frac = t1 / (t1 - t2);
	midf = p1f + frac*(p2f - p1f);
	mid[0] = p1[0] + frac*(p2[0] - p1[0]);
	mid[1] = p1[1] + frac*(p2[1] - p1[1]);
	mid[2] = p1[2] + frac*(p2[2] - p1[2]);
	RecurseProcBSP_r( results, areas, numAreas, maxAreas, nodeNum, node->children[side], p1f, midf, p1, mid );
	RecurseProcBSP_r( results, areas, numAreas, maxAreas, nodeNum, node->children[side^1], midf, p2f, mid, p2 );
}

/*
==================
idRenderWorldLocal::FastWorldTrace
==================
*/
bool idRenderWorldLocal::FastWorldTrace( modelTrace_t &results, const idVec3 &start, const idVec3 &end ) const {
	memset( &results, 0, sizeof( modelTrace_t ) );
	results.fraction = 1.0f;
	if ( areaNodes != NULL ) {
		RecurseProcBSP_r( &results, NULL, NULL, 0, -1, 0, 0.0f, 1.0f, start, end );
		return ( results.fraction < 1.0f );
	}
	return false;
}

/*
=================================================================================

CREATE MODEL REFS

=================================================================================
*/

/*
=================
AddEntityRefToArea

This is called by R_PushVolumeIntoTree and also directly
for the world model references that are precalculated.
=================
*/
void idRenderWorldLocal::AddEntityRefToArea( idRenderEntityLocal *def, portalArea_t *area ) {
	areaReference_t	*ref;

	if ( !def ) {
		common->Error( "idRenderWorldLocal::AddEntityRefToArea: NULL def" );
	} 
	else {
		ref = areaReferenceAllocator.Alloc();

		tr.pc.c_entityReferences++;

		ref->entity = def;

		// link to entityDef
		ref->ownerNext = def->entityRefs;
		def->entityRefs = ref;

		// link to end of area list
		ref->area = area;
		ref->areaNext = &area->entityRefs;
		ref->areaPrev = area->entityRefs.areaPrev;
		ref->areaNext->areaPrev = ref;
		ref->areaPrev->areaNext = ref;
	}
}

/*
===================
AddLightRefToArea

===================
*/
void idRenderWorldLocal::AddLightRefToArea( idRenderLightLocal *light, portalArea_t *area ) {
	areaReference_t	*lref;

	// add a lightref to this area
	lref = areaReferenceAllocator.Alloc();
	lref->light = light;
	lref->area = area;
	lref->ownerNext = light->references;
	light->references = lref;
	tr.pc.c_lightReferences++;

	// doubly linked list so we can free them easily later
	area->lightRefs.areaNext->areaPrev = lref;
	lref->areaNext = area->lightRefs.areaNext;
	lref->areaPrev = &area->lightRefs;
	area->lightRefs.areaNext = lref;
}

/*
===================
GenerateAllInteractions

Force the generation of all light / surface interactions at the start of a level
If this isn't called, they will all be dynamically generated

This really isn't all that helpful anymore, because the calculation of shadows
and light interactions is deferred from idRenderWorldLocal::CreateLightDefInteractions(), but we
use it as an oportunity to size the interactionTable
===================
*/
void idRenderWorldLocal::GenerateAllInteractions() {
	if ( !glConfig.isInitialized ) {
		return;
	}

#ifdef _DEBUG
	int start = Sys_Milliseconds();
#endif

	generateAllInteractionsCalled = false;

	// watch how much memory we allocate
	tr.staticAllocCount = 0;

	// let idRenderWorldLocal::CreateLightDefInteractions() know that it shouldn't
	// try and do any view specific optimizations
	tr.viewDef = NULL;

	for( int i = 0; i < this->lightDefs.Num(); i++ ) {
		idRenderLightLocal	*ldef = this->lightDefs[i];
		if( !ldef ) {
			continue;
		}
		this->CreateLightDefInteractions( ldef );
	}

#ifdef _DEBUG
	int end = Sys_Milliseconds();
	int	msec = end - start;

	common->Printf( "idRenderWorld::GenerateAllInteractions, msec = %i, staticAllocCount = %i.\n", msec, tr.staticAllocCount );
#endif

	// build the interaction table
	for( int i = 0; i < this->lightDefs.Num(); i++ ) {
		idRenderLightLocal *ldef = this->lightDefs[i];
		if( !ldef ) {
			continue;
		}
		for( idInteraction *inter = ldef->firstInteraction; inter != NULL; inter = inter->lightNext ) {
			idRenderEntityLocal	*edef = inter->entityDef;
			bool added = interactionTable.Add(inter);
		}
	}
	idStr stats = interactionTable.Stats();
	common->Printf( "Interaction table generated: %s\n", stats.c_str() );
	common->Printf( "Initial counts:  %d entities  %d lightDefs  %d entityDefs\n", gameLocal.num_entities, lightDefs.Num(), entityDefs.Num() );

	// entities flagged as noDynamicInteractions will no longer make any
	generateAllInteractionsCalled = true;
}

/*
===================
idRenderWorldLocal::FreeInteractions
===================
*/
void idRenderWorldLocal::FreeInteractions() {
	idRenderEntityLocal	*def;

	for (int i = 0 ; i < entityDefs.Num(); i++ ) {
		def = entityDefs[i];
		if ( !def ) {
			continue;
		}
		// free all the interactions
		while ( def->firstInteraction != NULL ) {
			def->firstInteraction->UnlinkAndFree();
		}
	}
}

/*
==================
idRenderWorldLocal::PushFrustumIntoTree_r

Used for both light volumes and model volumes.

This does not clip the points by the planes, so some slop
occurs.

tr.viewCount should be bumped before calling, allowing it
to prevent double checking areas.

We might alternatively choose to do this with an area flow.
==================
*/
void idRenderWorldLocal::PushFrustumIntoTree_r(idRenderEntityLocal* def, idRenderLightLocal* light,
	const frustumCorners_t& corners, int nodeNum)
{
	areaNode_t* node = areaNodes + nodeNum;

	// if we know that all possible children nodes only touch an area
	// we have already marked, we can early out
	if (node->commonChildrenArea != CHILDREN_HAVE_MULTIPLE_AREAS && r_useNodeCommonChildren.GetBool())
	{
		// note that we do NOT try to set a reference in this area
		// yet, because the test volume may yet wind up being in the
		// solid part, which would cause bounds slightly poked into
		// a wall to show up in the next room
		if ( portalAreas[node->commonChildrenArea].areaViewCount == tr.viewCount )
		{
			return;
		}
	}

	// exact check all the corners against the node plane
	frustumCull_t cull = idRenderMatrix::CullFrustumCornersToPlane(corners, node->plane);
	bool sideIncluded[2] = { cull != FRUSTUM_CULL_BACK, cull != FRUSTUM_CULL_FRONT };
	for ( int i = 0; i < 2; i++ ) {
		if ( !sideIncluded[i] )
			continue;
		nodeNum = node->children[i];
		if ( nodeNum == 0 )  	// 0 = solid
			continue;
		if ( nodeNum > 0 ) {	// navigate further down the BSP tree
			PushFrustumIntoTree_r( def, light, corners, nodeNum );
			continue;
		}						// tree leave
		int areaNum = -1 - nodeNum;
		portalArea_t* area = &portalAreas[areaNum];
		if ( area->areaViewCount == tr.viewCount ) {
			continue;	// already added a reference here
		}
		area->areaViewCount = tr.viewCount;
		if ( def != NULL ) {
			AddEntityRefToArea( def, area );
		}
		if ( light != NULL ) {
			AddLightRefToArea( light, area );
		}
	}
}

/*
==============
idRenderWorldLocal::PushFrustumIntoTree
==============
*/
void idRenderWorldLocal::PushFrustumIntoTree(idRenderEntityLocal* def, idRenderLightLocal* light, const idRenderMatrix& frustumTransform, const idBounds& frustumBounds)
{
	if (areaNodes == NULL)
		return;
	renderEntity_s::areaLock_t areaLock;
	// 2.08 Dragofer's draw call optimization
	if (def && (areaLock = def->parms.areaLock) != renderEntity_s::RAL_NONE && r_useAreaLocks & 1) { // 2.08 Dragofer's draw call optimization
		idVec3 point = areaLock == renderEntity_s::RAL_ORIGIN ? def->parms.origin : def->globalReferenceBounds.GetCenter();
		int areaNum = PointInArea(point);
		if (areaNum >= 0) {
			portalArea_t *area = &portalAreas[areaNum];
			AddEntityRefToArea(def, area);
			return;
		}
	}
	if (light && (areaLock = light->parms.areaLock) != renderEntity_s::RAL_NONE && r_useAreaLocks & 2) {
		idVec3 point = areaLock == renderEntity_s::RAL_ORIGIN ? light->parms.origin : light->globalLightBounds.GetCenter();
		int areaNum = PointInArea(point);
		if (areaNum >= 0) {
			portalArea_t *area = &portalAreas[areaNum];
			AddLightRefToArea(light, area);
			return;
		}
	}

	// calculate the corners of the frustum in word space
	ALIGNTYPE16 frustumCorners_t corners;
	idRenderMatrix::GetFrustumCorners(corners, frustumTransform, frustumBounds);

	PushFrustumIntoTree_r(def, light, corners, 0);
}

//===================================================================

/*
====================
idRenderWorldLocal::DebugClearLines
====================
*/
void idRenderWorldLocal::DebugClearLines( int time ) {
	R_ClearDebugLines( time );
	R_ClearDebugText( time );
}

/*
====================
idRenderWorldLocal::DebugLine
====================
*/
void idRenderWorldLocal::DebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifetime, const bool depthTest ) {
	R_AddDebugLine( color, start, end, lifetime, depthTest );
}

/*
================
idRenderWorldLocal::DebugArrow
================
*/
void idRenderWorldLocal::DebugArrow( const idVec4 &color, const idVec3 &start, const idVec3 &end, int size, const int lifetime ) {
	idVec3 forward, right, up, v1, v2;
	float a, s;
	int i;
	static float arrowCos[40];
	static float arrowSin[40];
	static int arrowStep;

	DebugLine( color, start, end, lifetime );

	if ( r_debugArrowStep.GetInteger() <= 10 ) {
		return;
	}
	// calculate sine and cosine when step size changes
	if ( arrowStep != r_debugArrowStep.GetInteger() ) {
		arrowStep = r_debugArrowStep.GetInteger();
		for (i = 0, a = 0; a < 360.0f; a += arrowStep, i++) {
			arrowCos[i] = idMath::Cos16( DEG2RAD( a ) );
			arrowSin[i] = idMath::Sin16( DEG2RAD( a ) );
		}
		arrowCos[i] = arrowCos[0];
		arrowSin[i] = arrowSin[0];
	}
	// draw a nice arrow
	forward = end - start;
	forward.Normalize();
	forward.NormalVectors( right, up);
	for (i = 0, a = 0; a < 360.0f; a += arrowStep, i++) {
		s = 0.5f * size * arrowCos[i];
		v1 = end - size * forward;
		v1 = v1 + s * right;
		s = 0.5f * size * arrowSin[i];
		v1 = v1 + s * up;

		s = 0.5f * size * arrowCos[i+1];
		v2 = end - size * forward;
		v2 = v2 + s * right;
		s = 0.5f * size * arrowSin[i+1];
		v2 = v2 + s * up;

		DebugLine( color, v1, end, lifetime );
		DebugLine( color, v1, v2, lifetime );
	}
}

/*
====================
idRenderWorldLocal::DebugWinding
====================
*/
void idRenderWorldLocal::DebugWinding( const idVec4 &color, const idWinding &w, const idVec3 &origin, const idMat3 &axis, const int lifetime, const bool depthTest ) {
	int i;
	idVec3 point, lastPoint;

	if ( w.GetNumPoints() < 2 ) {
		return;
	}

	lastPoint = origin + w[w.GetNumPoints()-1].ToVec3() * axis;
	for ( i = 0; i < w.GetNumPoints(); i++ ) {
		point = origin + w[i].ToVec3() * axis;
		DebugLine( color, lastPoint, point, lifetime, depthTest );
		lastPoint = point;
	}
}

/*
====================
idRenderWorldLocal::DebugCircle
====================
*/
void idRenderWorldLocal::DebugCircle( const idVec4 &color, const idVec3 &origin, const idVec3 &dir, const float radius, const int numSteps, const int lifetime, const bool depthTest ) {
	int i;
	float a;
	idVec3 left, up, point, lastPoint;

	dir.OrthogonalBasis( left, up );
	left *= radius;
	up *= radius;
	lastPoint = origin + up;
	for ( i = 1; i <= numSteps; i++ ) {
		a = idMath::TWO_PI * i / numSteps;
		point = origin + idMath::Sin16( a ) * left + idMath::Cos16( a ) * up;
		DebugLine( color, lastPoint, point, lifetime, depthTest );
		lastPoint = point;
	}
}

/*
============
idRenderWorldLocal::DebugSphere
============
*/
void idRenderWorldLocal::DebugSphere( const idVec4 &color, const idSphere &sphere, const int lifetime, const bool depthTest /*_D3XP*/ ) {
	int i, j, n, num;
	float s, c;
	idVec3 p, lastp, *lastArray;

	num = 360 / 15;
	lastArray = (idVec3 *) _alloca16( num * sizeof( idVec3 ) );
	lastArray[0] = sphere.GetOrigin() + idVec3( 0, 0, sphere.GetRadius() );
	for ( n = 1; n < num; n++ ) {
		lastArray[n] = lastArray[0];
	}

	for ( i = 15; i <= 360; i += 15 ) {
		s = idMath::Sin16( DEG2RAD(i) );
		c = idMath::Cos16( DEG2RAD(i) );
		lastp[0] = sphere.GetOrigin()[0];
		lastp[1] = sphere.GetOrigin()[1] + sphere.GetRadius() * s;
		lastp[2] = sphere.GetOrigin()[2] + sphere.GetRadius() * c;
		for ( n = 0, j = 15; j <= 360; j += 15, n++ ) {
			p[0] = sphere.GetOrigin()[0] + idMath::Sin16( DEG2RAD(j) ) * sphere.GetRadius() * s;
			p[1] = sphere.GetOrigin()[1] + idMath::Cos16( DEG2RAD(j) ) * sphere.GetRadius() * s;
			p[2] = lastp[2];

			DebugLine( color, lastp, p, lifetime,depthTest );
			DebugLine( color, lastp, lastArray[n], lifetime, depthTest );

			lastArray[n] = lastp;
			lastp = p;
		}
	}
}

/*
====================
idRenderWorldLocal::DebugBounds
====================
*/
void idRenderWorldLocal::DebugBounds( const idVec4 &color, const idBounds &bounds, const idVec3 &org, const int lifetime ) {
	int i;
	idVec3 v[8];

	if ( bounds.IsCleared() ) {
		return;
	}

	for ( i = 0; i < 8; i++ ) {
		v[i][0] = org[0] + bounds[(i^(i>>1))&1][0];
		v[i][1] = org[1] + bounds[(i>>1)&1][1];
		v[i][2] = org[2] + bounds[(i>>2)&1][2];
	}
	for ( i = 0; i < 4; i++ ) {
		DebugLine( color, v[i], v[(i+1)&3], lifetime );
		DebugLine( color, v[4+i], v[4+((i+1)&3)], lifetime );
		DebugLine( color, v[i], v[4+i], lifetime );
	}
}

/*
====================
idRenderWorldLocal::DebugBox
====================
*/
void idRenderWorldLocal::DebugBox( const idVec4 &color, const idBox &box, const int lifetime ) {
	int i;
	idVec3 v[8];

	box.ToPoints( v );
	for ( i = 0; i < 4; i++ ) {
		DebugLine( color, v[i], v[(i+1)&3], lifetime );
		DebugLine( color, v[4+i], v[4+((i+1)&3)], lifetime );
		DebugLine( color, v[i], v[4+i], lifetime );
	}
}

/*
================
idRenderWorldLocal::DebugFrustum
================
*/
void idRenderWorldLocal::DebugFrustum( const idVec4 &color, const idFrustum &frustum, const bool showFromOrigin, const int lifetime ) {
	int i;
	idVec3 v[8];

	frustum.ToPoints( v );

	if ( frustum.GetNearDistance() > 0.0f ) {
		for ( i = 0; i < 4; i++ ) {
			DebugLine( color, v[i], v[(i+1)&3], lifetime );
		}
		if ( showFromOrigin ) {
			for ( i = 0; i < 4; i++ ) {
				DebugLine( color, frustum.GetOrigin(), v[i], lifetime );
			}
		}
	}
	for ( i = 0; i < 4; i++ ) {
		DebugLine( color, v[4+i], v[4+((i+1)&3)], lifetime );
		DebugLine( color, v[i], v[4+i], lifetime );
	}
}

/*
============
idRenderWorldLocal::DebugCone

  dir is the cone axis
  radius1 is the radius at the apex
  radius2 is the radius at apex+dir
============
*/
void idRenderWorldLocal::DebugCone( const idVec4 &color, const idVec3 &apex, const idVec3 &dir, float radius1, float radius2, const int lifetime ) {
	int i;
	idMat3 axis;
	idVec3 top, p1, p2, lastp1, lastp2, d;

	axis[2] = dir;
	axis[2].Normalize();
	axis[2].NormalVectors( axis[0], axis[1] );
	axis[1] = -axis[1];

	top = apex + dir;
	lastp2 = top + radius2 * axis[1];

	if ( radius1 == 0.0f ) {
		for ( i = 20; i <= 360; i += 20 ) {
			d = idMath::Sin16( DEG2RAD(i) ) * axis[0] + idMath::Cos16( DEG2RAD(i) ) * axis[1];
			p2 = top + d * radius2;
			DebugLine( color, lastp2, p2, lifetime );
			DebugLine( color, p2, apex, lifetime );
			lastp2 = p2;
		}
	} else {
		lastp1 = apex + radius1 * axis[1];
		for ( i = 20; i <= 360; i += 20 ) {
			d = idMath::Sin16( DEG2RAD(i) ) * axis[0] + idMath::Cos16( DEG2RAD(i) ) * axis[1];
			p1 = apex + d * radius1;
			p2 = top + d * radius2;
			DebugLine( color, lastp1, p1, lifetime );
			DebugLine( color, lastp2, p2, lifetime );
			DebugLine( color, p1, p2, lifetime );
			lastp1 = p1;
			lastp2 = p2;
		}
	}
}

/*
================
idRenderWorldLocal::DebugAxis
================
*/
void idRenderWorldLocal::DebugAxis( const idVec3 &origin, const idMat3 &axis ) {
	idVec3 start = origin;
	idVec3 end = start + axis[0] * 20.0f;
	DebugArrow( colorWhite, start, end, 2 );
	end = start + axis[0] * -20.0f;
	DebugArrow( colorWhite, start, end, 2 );
	end = start + axis[1] * +20.0f;
	DebugArrow( colorGreen, start, end, 2 );
	end = start + axis[1] * -20.0f;
	DebugArrow( colorGreen, start, end, 2 );
	end = start + axis[2] * +20.0f;
	DebugArrow( colorBlue, start, end, 2 );
	end = start + axis[2] * -20.0f;
	DebugArrow( colorBlue, start, end, 2 );
}

/*
====================
idRenderWorldLocal::DebugClearPolygons
====================
*/
void idRenderWorldLocal::DebugClearPolygons( int time ) {
	R_ClearDebugPolygons( time );
}

/*
====================
idRenderWorldLocal::DebugPolygon
====================
*/
void idRenderWorldLocal::DebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest ) {
	R_AddDebugPolygon( color, winding, lifeTime, depthTest );
}

/*
================
idRenderWorldLocal::DebugScreenRect
================
*/
void idRenderWorldLocal::DebugScreenRect( const idVec4 &color, const idScreenRect &rect, const viewDef_t *viewDef, const int lifetime ) {
	int i;
	float centerx, centery, dScale, hScale, vScale;
	idBounds bounds;
	idVec3 p[4];

	centerx = ( viewDef->viewport.x2 - viewDef->viewport.x1 ) * 0.5f;
	centery = ( viewDef->viewport.y2 - viewDef->viewport.y1 ) * 0.5f;

	dScale = r_znear.GetFloat() + 1.0f;
	hScale = dScale * idMath::Tan16( DEG2RAD( viewDef->renderView.fov_x * 0.5f ) );
	vScale = dScale * idMath::Tan16( DEG2RAD( viewDef->renderView.fov_y * 0.5f ) );

	bounds[0][0] = bounds[1][0] = dScale;
	bounds[0][1] = -( rect.x1 - centerx ) / centerx * hScale;
	bounds[1][1] = -( rect.x2 - centerx ) / centerx * hScale;
	bounds[0][2] = ( rect.y1 - centery ) / centery * vScale;
	bounds[1][2] = ( rect.y2 - centery ) / centery * vScale;

	for ( i = 0; i < 4; i++ ) {
		p[i].x = bounds[0][0];
		p[i].y = bounds[(i^(i>>1))&1].y;
		p[i].z = bounds[(i>>1)&1].z;
		p[i] = viewDef->renderView.vieworg + p[i] * viewDef->renderView.viewaxis;
	}
	for ( i = 0; i < 4; i++ ) {
		DebugLine( color, p[i], p[(i+1)&3], false );
	}
}

/*
================
idRenderWorldLocal::DrawTextLength

  returns the length of the given text
================
*/
float idRenderWorldLocal::DrawTextLength( const char *text, float scale, int len ) {
	return R_DrawTextLength( text, scale, len );
}

/*
================
idRenderWorldLocal::DrawText

  oriented on the viewaxis
  align can be 0-left, 1-center (default), 2-right
================
*/
void idRenderWorldLocal::DrawText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime, const bool depthTest ) {
	R_AddDebugText( text, origin, scale, color, viewAxis, align, lifetime, depthTest );
}

/*
===============
idRenderWorldLocal::RegenerateWorld
===============
*/
void idRenderWorldLocal::RegenerateWorld() {
	R_RegenerateWorld_f( idCmdArgs() );
}

/*
===============
R_GlobalShaderOverride
===============
*/
bool R_GlobalShaderOverride( const idMaterial **shader ) {

	if ( !(*shader)->IsDrawn() ) {
		return false;
	}

	if ( tr.primaryRenderView.globalMaterial ) {
		*shader = tr.primaryRenderView.globalMaterial;
		return true;
	}

	if ( r_materialOverride.GetString()[0] != '\0' ) {
		*shader = declManager->FindMaterial( r_materialOverride.GetString() );
		return true;
	}
	
	return false;
}

/*
===============
R_RemapShaderBySkin
===============
*/
const idMaterial *R_RemapShaderBySkin( const idMaterial *shader, const idDeclSkin *skin, const idMaterial *customShader ) {

	// This is unlikely and fatal
	/*if ( !shader ) {
		return NULL;
	}*/
		
	// early exit if we arnt doing anything
	// never remap surfaces that were originally nodraw, like collision hulls
	if ( !skin || !shader->IsDrawn()) {
		return shader;
	}

	// Note: This does not seem to be evaluated ever, tho there is reasoning below for its inclusion.
	// FIXME : If no reports, comment him out.
	if ( customShader ) {
		common->Warning("Skin CustomShader. Please report this.");
		// this is sort of a hack, but cause deformed surfaces to map to empty surfaces,
		// so the item highlight overlay doesn't highlight the autosprite surface
		if ( shader->Deform() ) {
			return NULL;
		}
		return customShader;
	}

	return skin->RemapShaderBySkin( shader );
}

idVec3 getBarycentricCoordinatesAt( const idVec3 &P, const idVec3 &a, const idVec3 &b, const idVec3 &c ) {
	idVec3 bary;
	idPlane triPlane( a, b, c );

	// The area of a triangle is 
	auto areaABC = (b - a).Cross(c - a) * triPlane.Normal();
	auto areaPBC = (b - P).Cross(c - P) * triPlane.Normal();
	auto areaPCA = (c - P).Cross(a - P) * triPlane.Normal();

	bary.x = areaPBC / areaABC; // alpha
	bary.y = areaPCA / areaABC; // beta
	bary.z = 1.0f - bary.x - bary.y; // gamma

	return bary;
}

/*
===============
idRenderWorldLocal::MaterialTrace
Try to remap materials on a per-pixel basis
Supposed to be used by game/physics code
===============
*/
bool idRenderWorldLocal::MaterialTrace( const idVec3 &p, const idMaterial *mat, idStr &matName ) const {
	// only testing the collision area - what if blocker is <.25 units behind the area end?
	int areaNum = PointInArea( p );
	if ( areaNum < 0 )
		return false;
	auto area = &portalAreas[areaNum];
	for ( auto eref = area->entityRefs.areaNext; eref != &area->entityRefs; eref = eref->areaNext ) {
		auto def = eref->entity;	// loop through all entities in the area, skipping some
		if ( def->dynamicModel )
			continue;
		idVec3 lp;
		R_GlobalPointToLocal( def->modelMatrix, p, lp );
		if ( !def->globalReferenceBounds.ContainsPoint( p ) )
			continue;				// per-entity bounds check
		auto model = def->parms.hModel;
		int total = model->NumSurfaces();
		for ( int i = 0; i < total; i++ ) {
			auto surf = model->Surface( i );
			auto geo = surf->geometry;
			if ( mat != surf->material )
				continue;			// we already know from the collision manager what material it should be
			if ( !geo->bounds.ContainsPoint( lp ) )
				continue;			// per-surface bounds check
			for ( int vInd = 0; vInd < geo->numIndexes; ) { // as bad as it is we're looping through the vertices
				auto &v1 = geo->verts[geo->indexes[vInd++]];
				auto &v2 = geo->verts[geo->indexes[vInd++]];
				auto &v3 = geo->verts[geo->indexes[vInd++]];
				auto v12 = v2.xyz - v1.xyz;
				auto v13 = v3.xyz - v1.xyz;
				idPlane triPlane( v1.xyz, v2.xyz, v3.xyz );
				float d = triPlane.Distance( lp );
				if ( abs( d ) > 2 * CM_CLIP_EPSILON ) // FIXME is 2x necessary?
					continue;						  // too far from collision point
				auto bari = getBarycentricCoordinatesAt( lp, v1.xyz, v2.xyz, v3.xyz );
				auto st = bari.x * v1.st + bari.y * v2.st + bari.z * v3.st;
				static byte* pic = NULL;			  // FIXME check if lp is INSIDE the triangle
				static int width, height;
				if ( !pic )							  // FIXME this needs to be part of material?
					R_LoadImageProgram( mat->GetMaterialImage(), &pic, &width, &height, NULL );
				if ( !pic ) 
					return false;
				int pixNum = st.x * (width-1) + st.y * (height-1) * width;	// FIXME clamp texcoords
				auto red = pic[pixNum * 3];									// FIXME use color table
				if ( red > 99 )
					matName = "metal";
				common->Printf( "material map (%.2fx%.2f) %d - %s\n", st.x, st.y, red, matName.c_str() );
				return true;
			}
		}
	}
	return false;
}