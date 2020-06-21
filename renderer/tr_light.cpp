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

#include "../idlib/geometry/sys_intrinsics.h"
#include "tr_local.h"
#include "Model_local.h" // Added in #3878 (soft particles) to allow r_AddAmbientDrawSurfs to access info about particles to 
#include "Profiling.h"
// pass to the backend without bloating the modelSurface_t struct used everywhere. That struct is the only
						 // output of ALL dynamic model updates, and it's a POD (non-initialized), so adding the info to it would 
						 // mean initializing it, or adding code to every single dynamic model update function. Model_local.h 
						 // has no #defines and adds no includes of its own, and tr_light.cpp already has sight of DeclParticle.h 
						 // so I reckon this extra dependency is lightweight and justified. -- SteveL #3878

#define CHECK_BOUNDS_EPSILON			1.0f

idCVar r_maxShadowMapLight( "r_maxShadowMapLight", "1000", CVAR_ARCHIVE | CVAR_RENDERER, "lights bigger than this will be force-sent to stencil" );
idCVar r_useParallelAddModels( "r_useParallelAddModels", "0", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "add all models in parallel with jobs" );
idCVarBool r_useClipPlaneCulling( "r_useClipPlaneCulling", "1", CVAR_RENDERER, "cull surfaces behind mirrors" );

/*
===========================================================================================

VERTEX CACHE GENERATORS

===========================================================================================
*/

/*
==================
R_CreateAmbientCache

Create it if needed
==================
*/
bool R_CreateAmbientCache( srfTriangles_t *tri, bool needsLighting ) {
	if ( vertexCache.CacheIsCurrent( tri->ambientCache ) ) {
		return true;
	}

	// we are going to use it for drawing, so make sure we have the tangents and normals
	if ( needsLighting && !tri->tangentsCalculated ) {
		R_DeriveTangents( tri );
	}
	tri->ambientCache = vertexCache.AllocVertex( tri->verts, tri->numVerts * sizeof( tri->verts[0] ) );

	return tri->ambientCache.IsValid();
}

/*
==================
R_CreatePrivateShadowCache

This is used only for a specific light
==================
*/
void R_CreatePrivateShadowCache( srfTriangles_t *tri ) {

	if ( vertexCache.CacheIsCurrent( tri->shadowCache ) ) {
		return;
	}
	tri->shadowCache = vertexCache.AllocVertex( tri->shadowVertexes, tri->numVerts * sizeof( *tri->shadowVertexes ) );
}

/*
==================
R_CreateVertexProgramShadowCache

This is constant for any number of lights, the vertex program
takes care of projecting the verts to infinity.
==================
*/
void R_CreateVertexProgramShadowCache( srfTriangles_t *tri ) {
	if ( !tri->verts ) {
		return;
	}
	shadowCache_t *temp = (shadowCache_t *)_alloca16( tri->numVerts * 2 * sizeof( shadowCache_t ) );
	SIMDProcessor->CreateVertexProgramShadowCache( &temp->xyz, tri->verts, tri->numVerts );
	tri->shadowCache = vertexCache.AllocVertex( temp, ALIGN( tri->numVerts * 2 * sizeof( shadowCache_t ), VERTEX_CACHE_ALIGN ) );
}

/*
==================
R_SkyboxTexGen
==================
*/
//void R_SkyboxTexGen( drawSurf_t *surf, const idVec3 &viewOrg ) {
//	idVec3	localViewOrigin;
//
//	R_GlobalPointToLocal( surf->space->modelMatrix, viewOrg, localViewOrigin );
//
//	const idDrawVert *verts = surf->frontendGeo->verts;
//	const int numVerts = surf->frontendGeo->numVerts;
//	const int size = numVerts * sizeof( idVec3 );
//	idVec3 *texCoords = (idVec3 *) _alloca16( size );
//
//	for ( int i = 0; i < numVerts; i++ ) {
//		texCoords[i][0] = verts[i].xyz[0] - localViewOrigin[0];
//		texCoords[i][1] = verts[i].xyz[1] - localViewOrigin[1];
//		texCoords[i][2] = verts[i].xyz[2] - localViewOrigin[2];
//	}
//	surf->dynamicTexCoords = vertexCache.AllocVertex( texCoords, ALIGN( size, VERTEX_CACHE_ALIGN ) );
//}

/*
==================
R_WobbleskyTexGen
==================
*/
//void R_WobbleskyTexGen( drawSurf_t *surf, const idVec3 &viewOrg ) {
//	idVec3	localViewOrigin;
//
//	const int *parms = surf->material->GetTexGenRegisters();
//
//	const float	wobbleDegrees = surf->shaderRegisters[ parms[0] ] * idMath::PI / 180.0f;
//	const float	wobbleSpeed   = surf->shaderRegisters[ parms[1] ] * 2.0f * idMath::PI / 60.0f;
//	const float	rotateSpeed   = surf->shaderRegisters[ parms[2] ] * 2.0f * idMath::PI / 60.0f;
//
//	// very ad-hoc "wobble" transform
//	float	transform[16];
//	const float	a = tr.viewDef->floatTime * wobbleSpeed;
//	const float	z = cos( wobbleDegrees );
//	float	s = sin( a ) * sin( wobbleDegrees );
//	float	c = cos( a ) * sin( wobbleDegrees );
//
//	idVec3	axis[3];
//
//	axis[2][0] = c;
//	axis[2][1] = s;
//	axis[2][2] = z;
//
//	axis[1][0] = -sin( a * 2.0f ) * sin( wobbleDegrees );
//	axis[1][2] = -s * sin( wobbleDegrees );
//	axis[1][1] = sqrt( 1.0f - ( axis[1][0] * axis[1][0] + axis[1][2] * axis[1][2] ) );
//
//	// make the second vector exactly perpendicular to the first
//	axis[1] -= ( axis[2] * axis[1] ) * axis[2];
//	axis[1].Normalize();
//
//	// construct the third with a cross
//	axis[0].Cross( axis[1], axis[2] );
//
//	// add the rotate
//	s = sin( rotateSpeed * tr.viewDef->floatTime );
//	c = cos( rotateSpeed * tr.viewDef->floatTime );
//
//	transform[0] = axis[0][0] * c + axis[1][0] * s;
//	transform[4] = axis[0][1] * c + axis[1][1] * s;
//	transform[8] = axis[0][2] * c + axis[1][2] * s;
//
//	transform[1] = axis[1][0] * c - axis[0][0] * s;
//	transform[5] = axis[1][1] * c - axis[0][1] * s;
//	transform[9] = axis[1][2] * c - axis[0][2] * s;
//
//	transform[2] = axis[2][0];
//	transform[6] = axis[2][1];
//	transform[10] = axis[2][2];
//
//	transform[3] = transform[7] = transform[11] = 0.0f;
//	transform[12] = transform[13] = transform[14] = 0.0f;
//
//	R_GlobalPointToLocal( surf->space->modelMatrix, viewOrg, localViewOrigin );
//
//	const int numVerts = surf->frontendGeo->numVerts;
//	const int size = numVerts * sizeof( idVec3 );
//	idVec3 *texCoords = (idVec3 *) _alloca16( size );
//	const idDrawVert *verts = surf->frontendGeo->verts;
//	idVec3 v;
//
//	for (int i = 0; i < numVerts; i++ ) {
//		v[0] = verts[i].xyz[0] - localViewOrigin[0];
//		v[1] = verts[i].xyz[1] - localViewOrigin[1];
//		v[2] = verts[i].xyz[2] - localViewOrigin[2];
//
//		R_LocalPointToGlobal( transform, v, texCoords[i] );
//	}
//	surf->dynamicTexCoords = vertexCache.AllocVertex( texCoords, ALIGN( size, VERTEX_CACHE_ALIGN ) );
//}

//=======================================================================================================

/*
=============
R_SetEntityDefViewEntity

If the entityDef isn't already on the viewEntity list, create
a viewEntity and add it to the list with an empty scissor rect.

This does not instantiate dynamic models for the entity yet.
=============
*/
viewEntity_t *R_SetEntityDefViewEntity( idRenderEntityLocal *def ) {
	viewEntity_t		*vModel;

	if ( def->viewCount == tr.viewCount ) {
		return def->viewEntity;
	}
	def->viewCount = tr.viewCount;

	// set the model and modelview matricies
	vModel = (viewEntity_t *)R_ClearedFrameAlloc( sizeof( *vModel ) );
	vModel->entityDef = def;

	// the scissorRect will be expanded as the model bounds is accepted into visible portal chains
	vModel->scissorRect.Clear();

	// copy the model and weapon depth hack for back-end use
	vModel->modelDepthHack = def->parms.modelDepthHack;
	vModel->weaponDepthHack = def->parms.weaponDepthHack;

	R_AxisToModelMatrix( def->parms.axis, def->parms.origin, vModel->modelMatrix );

	// we may not have a viewDef if we are just creating shadows at entity creation time
	if ( tr.viewDef ) {
		myGlMultMatrix( vModel->modelMatrix, tr.viewDef->worldSpace.modelViewMatrix, vModel->modelViewMatrix );

		vModel->next = tr.viewDef->viewEntitys;
		tr.viewDef->viewEntitys = vModel;
	}
	def->viewEntity = vModel;

	return vModel;
}

/*
====================
R_TestPointInViewLight
====================
*/
static const float INSIDE_LIGHT_FRUSTUM_SLOP = 32.0f;
// this needs to be greater than the dist from origin to corner of near clip plane
static bool R_TestPointInViewLight( const idVec3 &org, const idRenderLightLocal *light ) {

	for ( int i = 0 ; i < 6 ; i++ ) {
		if ( light->frustum[i].Distance( org ) > INSIDE_LIGHT_FRUSTUM_SLOP ) {
			return false;
		}
	}
	return true;
}

/*
===================
R_PointInFrustum

Assumes positive sides face outward
===================
*/
static bool R_PointInFrustum( idVec3 &p, idPlane *planes, int numPlanes ) {

	for ( int i = 0 ; i < numPlanes ; i++ ) {
		if ( planes[i].Distance( p ) > 0.0f ) {
			return false;
		}
	}
	return true;
}

/*
=============
R_SetLightDefViewLight

If the lightDef isn't already on the viewLight list, create
a viewLight and add it to the list with an empty scissor rect.
=============
*/
viewLight_t *R_SetLightDefViewLight( idRenderLightLocal *light ) {
	viewLight_t *vLight;

	if ( light->viewCount == tr.viewCount ) {
		return light->viewLight;
	}
	light->viewCount = tr.viewCount;

	// add to the view light chain
	vLight = (viewLight_t *)R_ClearedFrameAlloc( sizeof( *vLight ) );
	vLight->lightDef = light;
	vLight->pointLight = light->parms.pointLight;
	vLight->radius = light->parms.radius;
	vLight->noShadows = light->parms.noShadows;
	vLight->noSpecular = light->parms.noSpecular;

	// the scissorRect will be expanded as the light bounds is accepted into visible portal chains
	vLight->scissorRect.Clear();

	// calculate the shadow cap optimization states
	vLight->viewInsideLight = R_TestPointInViewLight( tr.viewDef->renderView.vieworg, light );
	if ( !vLight->viewInsideLight ) {
		vLight->viewSeesShadowPlaneBits = 0;
		for ( int i = 0 ; i < light->numShadowFrustums ; i++ ) {
			float d = light->shadowFrustums[i].planes[5].Distance( tr.viewDef->renderView.vieworg );
			if ( d < INSIDE_LIGHT_FRUSTUM_SLOP ) {
				vLight->viewSeesShadowPlaneBits|= 1 << i;
			}
		}
	} else {
		// this should not be referenced in this case
		vLight->viewSeesShadowPlaneBits = 63;
	}

	// see if the light center is in view, which will allow us to cull invisible shadows
	vLight->viewSeesGlobalLightOrigin = R_PointInFrustum( light->globalLightOrigin, tr.viewDef->frustum, 4 );

	// copy data used by backend
	vLight->globalLightOrigin = light->globalLightOrigin;
	vLight->lightProject[0] = light->lightProject[0];
	vLight->lightProject[1] = light->lightProject[1];
	vLight->lightProject[2] = light->lightProject[2];
	vLight->lightProject[3] = light->lightProject[3];

	// the fog plane is the light far clip plane
	idPlane fogPlane(light->baseLightProject[2][0] - light->baseLightProject[3][0],
		light->baseLightProject[2][1] - light->baseLightProject[3][1],
		light->baseLightProject[2][2] - light->baseLightProject[3][2],
		light->baseLightProject[2][3] - light->baseLightProject[3][3]);
	const float planeScale = idMath::InvSqrt(fogPlane.Normal().LengthSqr());
	vLight->fogPlane[0] = fogPlane[0] * planeScale;
	vLight->fogPlane[1] = fogPlane[1] * planeScale;
	vLight->fogPlane[2] = fogPlane[2] * planeScale;
	vLight->fogPlane[3] = fogPlane[3] * planeScale;

	// make a copy of the frustum for backend rendering
	vLight->frustumTris = ( srfTriangles_t* )R_FrameAlloc( sizeof( srfTriangles_t ) );
	memcpy( vLight->frustumTris, light->frustumTris, sizeof( srfTriangles_t ) );
	vLight->falloffImage = light->falloffImage;
	vLight->lightShader = light->lightShader;
	vLight->shaderRegisters = NULL;		// allocated and evaluated in R_AddLightSurfaces
	vLight->noFogBoundary = light->parms.noFogBoundary; // #3664

	auto shader = vLight->lightShader;
	auto tooBigForShadowMaps = ( (light->parms.lightRadius.Length() > r_maxShadowMapLight.GetFloat()) || ( light->parms.parallel ) );
	if ( !r_shadows.GetInteger() || !shader->LightCastsShadows() )
		vLight->shadows = LS_NONE;
	else
		if ( r_shadows.GetInteger() == 1 || tooBigForShadowMaps )
			vLight->shadows = LS_STENCIL;
		else
			vLight->shadows = LS_MAPS;

	// multi-light shader stuff
	if ( shader->LightCastsShadows() && tooBigForShadowMaps ) // use stencil shadows
		vLight->singleLightOnly = true;
	if ( shader->IsAmbientLight() ) { // custom ambient projection
		vLight->singleLightOnly = !strstr( shader->GetName(), "ambientlightnfo" )
			&& !strstr( shader->GetName(), "ambient_biground" );
	}
	if ( !shader->IsAmbientLight() && !strstr( shader->GetName(), "biground" ) ) // custom point light projection
		vLight->singleLightOnly = true;
	if(vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight())
		vLight->singleLightOnly = true;

	// link the view light
	vLight->next = tr.viewDef->viewLights;
	tr.viewDef->viewLights = vLight;
	light->viewLight = vLight;

	return vLight;
}

/*
=================
idRenderWorldLocal::CreateLightDefInteractions

When a lightDef is determined to effect the view (contact the frustum and non-0 light), it will check to
make sure that it has interactions for all the entityDefs that it might possibly contact.

This does not guarantee that all possible interactions for this light are generated, only that
the ones that may effect the current view are generated. so it does need to be called every view.

This does not cause entityDefs to create dynamic models, all work is done on the referenceBounds.

All entities that have non-empty interactions with viewLights will
have viewEntities made for them and be put on the viewEntity list,
even if their surfaces aren't visible, because they may need to cast shadows.

Interactions are usually removed when a entityDef or lightDef is modified, unless the change
is known to not effect them, so there is no danger of getting a stale interaction, we just need to
check that needed ones are created.

An interaction can be at several levels:

Don't interact (but share an area) (numSurfaces = 0)
Entity reference bounds touches light frustum, but surfaces haven't been generated (numSurfaces = -1)
Shadow surfaces have been generated, but light surfaces have not.  The shadow surface may still be empty due to bounds being conservative.
Both shadow and light surfaces have been generated.  Either or both surfaces may still be empty due to conservative bounds.

=================
*/
void idRenderWorldLocal::CreateLightDefInteractions( idRenderLightLocal *ldef ) {

	bool lightCastsShadows = ldef->lightShader->LightCastsShadows();

	for ( areaReference_t *lref = ldef->references ; lref ; lref = lref->ownerNext ) {
		portalArea_t *area = lref->area;

		// check all the models in this area
		for ( areaReference_t *eref = area->entityRefs.areaNext ; eref != &area->entityRefs ; eref = eref->areaNext ) {
			idRenderEntityLocal	*edef = eref->entity;

			// if the entity doesn't have any light-interacting surfaces, we could skip this,
			// but we don't want to instantiate dynamic models yet, so we can't check that on
			// most things

			if ( tr.viewDef ) {
				// if the entity isn't viewed and light has now shadows, skip
				if ( !lightCastsShadows ) {
					if ( edef->viewCount != tr.viewCount ) {
						continue;
					}
				}
				// if the entity isn't viewed and shadow is suppressed, skip
				if ( edef->parms.suppressShadowInViewID && edef->parms.suppressShadowInViewID == tr.viewDef->renderView.viewID ) {
					if ( !r_skipSuppress.GetBool() && edef->viewCount != tr.viewCount ) {
						continue;
					}
				}
				if ( edef->parms.suppressShadowInLightID && edef->parms.suppressShadowInLightID == ldef->parms.lightId ) {
					if ( !r_skipSuppress.GetBool() && edef->viewCount != tr.viewCount ) {
						continue;
					}
				}
			}

			// some big outdoor meshes are flagged to not create any dynamic interactions
			// when the level designer knows that nearby moving lights shouldn't actually hit them
			if ( edef->parms.noDynamicInteractions && edef->world->generateAllInteractionsCalled ) {
				continue;
			}

			if ( r_singleEntity.GetInteger() >= 0 && r_singleEntity.GetInteger() != edef->index ) {
				continue;
			}
			
			// if any of the edef's interaction match this light, we don't
			// need to consider it. 
			idInteraction *inter = interactionTable.Find(ldef, edef);
			if ( inter ) {
				// if this entity wasn't in view already, the scissor rect will be empty,
				// so it will only be used for shadow casting
				if ( !inter->IsEmpty() ) {
					R_SetEntityDefViewEntity( edef );
				}
				continue;
			}

			// create a new interaction, but don't do any work other than bbox to frustum culling
			inter = idInteraction::AllocAndLink( edef, ldef );

			// do a check of the entity reference bounds against the light frustum,
			// trying to avoid creating a viewEntity if it hasn't been already
			float	modelMatrix[16];
			float	*m;
			if ( edef->viewCount == tr.viewCount ) {
				m = edef->viewEntity->modelMatrix;
			} else {
				R_AxisToModelMatrix( edef->parms.axis, edef->parms.origin, modelMatrix );
				m = modelMatrix;
			}

			if ( R_CullLocalBox( edef->referenceBounds, m, 6, ldef->frustum ) ) {
				inter->MakeEmpty();
				continue;
			}

			// we will do a more precise per-surface check when we are checking the entity
			// if this entity wasn't in view already, the scissor rect will be empty,
			// so it will only be used for shadow casting
			R_SetEntityDefViewEntity( edef );
		}
	}
}

static const int INTERACTION_TABLE_MAX_LIGHTS = 4096;
static const int INTERACTION_TABLE_MAX_ENTITYS = MAX_GENTITIES;
idInteractionTable::idInteractionTable() {
	SM_matrix = nullptr;
}
idInteractionTable::~idInteractionTable() {
	FreeMemory();
}
void idInteractionTable::Init() {
	if (r_useInteractionTable.GetInteger() == 1) {
		delete[] SM_matrix;
		SM_matrix = (idInteraction**)R_ClearedStaticAlloc(INTERACTION_TABLE_MAX_LIGHTS * INTERACTION_TABLE_MAX_ENTITYS * sizeof(SM_matrix[0]));
		if (!SM_matrix) {
			common->Error("Failed to allocate interaction table");
		}
	}
	if (r_useInteractionTable.GetInteger() == 2) {
		static const int MAX_INTERACTION_TABLE_LOAD_FACTOR = 75;
		SHT_table.Init(-1, MAX_INTERACTION_TABLE_LOAD_FACTOR);
	}
}
void idInteractionTable::FreeMemory() {
	if (r_useInteractionTable.GetInteger() == 1) {
		delete[] SM_matrix;
	}
	if (r_useInteractionTable.GetInteger() == 2) {
		SHT_table.Reset();
	}
}
DEBUG_OPTIMIZE_ON
idInteraction *idInteractionTable::Find(idRenderLightLocal *ldef, idRenderEntityLocal *edef) const {
	if (r_useInteractionTable.GetInteger() == 1) {
		int idx = ldef->index * INTERACTION_TABLE_MAX_ENTITYS + edef->index;
		return SM_matrix[idx];
	}
	if (r_useInteractionTable.GetInteger() == 2) {
		int key = (ldef->index << 16) + edef->index;
		const auto &cell = const_cast<idInteractionTable*>(this)->SHT_table.Find( key );
		idInteraction *inter = nullptr;
		if ( !SHT_table.IsEmpty( cell ) ) {
			inter = cell.value;
		}
		return inter;
	}
	for ( idInteraction *inter = edef->lastInteraction; inter; inter = inter->entityPrev ) {
		if ( inter->lightDef == ldef ) {
			return inter;
		}
	}
	return nullptr;
}
DEBUG_OPTIMIZE_OFF
bool idInteractionTable::Add(idInteraction *interaction) {
	if (r_useInteractionTable.GetInteger() == 1) {
		assert(interaction->lightDef->index < INTERACTION_TABLE_MAX_LIGHTS);
		assert(interaction->entityDef->index < INTERACTION_TABLE_MAX_ENTITYS);
		int idx = interaction->lightDef->index * INTERACTION_TABLE_MAX_ENTITYS + interaction->entityDef->index;
		idInteraction *&cell = SM_matrix[idx];
		if (cell) {
			return false;
		}
		cell = interaction;
		return true;
	}
	if (r_useInteractionTable.GetInteger() == 2) {
		int key = ( interaction->lightDef->index << 16 ) + interaction->entityDef->index;
		auto &cell = SHT_table.Find( key );
		if ( !SHT_table.IsEmpty( cell ) ) {
			return false;	//such interaction already exists
		}
		cell.key = key;
		cell.value = interaction;
		SHT_table.Added( cell );
		return true;	//added new interaction
	}
	return true;	//don't care
}
bool idInteractionTable::Remove(idInteraction *interaction) {
	if (r_useInteractionTable.GetInteger() == 1) {
		int idx = interaction->lightDef->index * INTERACTION_TABLE_MAX_ENTITYS + interaction->entityDef->index;
		idInteraction *&cell = SM_matrix[idx];
		if (cell) {
			assert(cell == interaction);
			cell = nullptr;
			return true;
		}
		return false;
	}
	if (r_useInteractionTable.GetInteger() == 2) {
		int key = ( interaction->lightDef->index << 16 ) + interaction->entityDef->index;
		auto &cell = SHT_table.Find( key );
		if ( cell.key == key ) {
			assert( cell.value == interaction );
			SHT_table.Erase( cell );
			return true;	//removed previously existing interaction
		}
		return false;		//such interaction not present
	}
	return true;	//don't care
}
idStr idInteractionTable::Stats() const {
	char buff[256];
	if (r_useInteractionTable.GetInteger() == 1) {
		idStr::snPrintf(buff, sizeof(buff), "size = L%d x E%d x %dB = %d MB",
			INTERACTION_TABLE_MAX_LIGHTS, INTERACTION_TABLE_MAX_ENTITYS, sizeof(SM_matrix[0]),
			INTERACTION_TABLE_MAX_LIGHTS * INTERACTION_TABLE_MAX_ENTITYS * sizeof(SM_matrix[0])
		);
	}
	if (r_useInteractionTable.GetInteger() == 2) {
		idStr::snPrintf(buff, sizeof(buff), "size = %d/%d", SHT_table.Count(), SHT_table.Size());
	}
	return buff;
}

//===============================================================================================================

/*
=================
R_LinkLightSurf
=================
*/
void R_LinkLightSurf( drawSurf_t **link, const srfTriangles_t *tri, const viewEntity_t *space,
		const idMaterial *material, const idScreenRect &scissor, bool viewInsideShadow ) {
	if ( !space ) {
		space = &tr.viewDef->worldSpace;
	}
	drawSurf_t *drawSurf = (drawSurf_t *)R_FrameAlloc( sizeof( *drawSurf ) );

	drawSurf->CopyGeo( tri );
	drawSurf->space = space;
	drawSurf->material = material;
	drawSurf->scissorRect = scissor;
	drawSurf->dsFlags = scissor.IsEmpty() ? DSF_SHADOW_MAP_ONLY : 0;
	if ( space->entityDef && space->entityDef->parms.noShadow || !material || !material->SurfaceCastsShadow() ) { // some dynamic models use a no-shadow material and for shadows have a separate geometry with an invisible (in main render) material
		drawSurf->dsFlags |= DSF_SHADOW_MAP_IGNORE;
	}
	
	static idCVar r_skipDynamicShadows( "r_skipDynamicShadows", "0", CVAR_ARCHIVE | CVAR_BOOL | CVAR_RENDERER, "" );
	if ( r_skipDynamicShadows.GetBool() )
		for ( auto ent = space; ent; ent = ent->next ) {
			//&& !space->entityDef->parms.hModel->IsStaticWorldModel() 
			//	&& space->entityDef->lastModifiedFrameNum == tr.viewCount 
			if ( ent->entityDef && ent->entityDef->parms.hModel && ent->entityDef->parms.hModel->IsDynamicModel() ) {
				drawSurf->dsFlags |= DSF_SHADOW_MAP_IGNORE;
			}
		}

	drawSurf->particle_radius = 0.0f; // #3878

	if ( viewInsideShadow ) {
		drawSurf->dsFlags |= DSF_VIEW_INSIDE_SHADOW;
	}

	if ( !material ) {
		// shadows won't have a shader
		drawSurf->shaderRegisters = NULL;
		if ( !(drawSurf->dsFlags & DSF_VIEW_INSIDE_SHADOW) )
			drawSurf->numIndexes = tri->numShadowIndexesNoCaps;
	} else {
		// process the shader expressions for conditionals / color / texcoords
		const float *constRegs = material->ConstantRegisters();
		if ( constRegs ) {
			// this shader has only constants for parameters
			drawSurf->shaderRegisters = constRegs;
		} else {
			// FIXME: share with the ambient surface?
			float *regs = (float *)R_FrameAlloc( material->GetNumRegisters() * sizeof( float ) );
			drawSurf->shaderRegisters = regs;
			material->EvaluateRegisters( regs, space->entityDef->parms.shaderParms, tr.viewDef, space->entityDef->parms.referenceSound );
		}
	}

	Sys_EnterCriticalSection( CRITICAL_SECTION_TWO );
	// actually link it in
	drawSurf->nextOnLight = *link;
	*link = drawSurf;
	Sys_LeaveCriticalSection( CRITICAL_SECTION_TWO );
}

/*
======================
R_ClippedLightScissorRectangle
======================
*/
idScreenRect R_ClippedLightScissorRectangle( viewLight_t *vLight ) {
	const idRenderLightLocal *light = vLight->lightDef;
	idScreenRect r;
	idFixedWinding w;

	r.Clear();

	for ( int i = 0 ; i < 6 ; i++ ) {
		const idWinding &ow = light->frustumWindings[i];

		// !ow - projected lights may have one of the frustums degenerated
		// OR
		// light->frustum[i].Distance( tr.viewDef->renderView.vieworg ) >= 0
		// the light frustum planes face out from the light,
		// so the planes that have the view origin on the negative
		// side will be the "back" faces of the light, which must have
		// some fragment inside the portalStack to be visible
		if ( !ow.GetNumPoints() || light->frustum[i].Distance( tr.viewDef->renderView.vieworg ) >= 0 ) {
			continue;
		}
		w = ow;

		// now check the winding against each of the frustum planes
		for ( int j = 0; j < 5; j++ ) {
			if ( !w.ClipInPlace( -tr.viewDef->frustum[j] ) ) {
				break;
			}
		}

		// project these points to the screen and add to bounds
		for ( int j = 0 ; j < w.GetNumPoints(); j++ ) {
			idPlane		eye, clip;
			idVec3		ndc;

			R_TransformModelToClip( w[j].ToVec3(), tr.viewDef->worldSpace.modelViewMatrix, tr.viewDef->projectionMatrix, eye, clip );

			if ( clip[3] <= 0.01f ) {
				clip[3] = 0.01f;
			}
			R_TransformClipToDevice( clip, tr.viewDef, ndc );

			float windowX = 0.5f * ( 1.0f + ndc[0] ) * ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 );
			float windowY = 0.5f * ( 1.0f + ndc[1] ) * ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 );

			if ( windowX > tr.viewDef->scissor.x2 ) {
				windowX = tr.viewDef->scissor.x2;
			} else if ( windowX < tr.viewDef->scissor.x1 ) {
				windowX = tr.viewDef->scissor.x1;
			}
			if ( windowY > tr.viewDef->scissor.y2 ) {
				windowY = tr.viewDef->scissor.y2;
			} else if ( windowY < tr.viewDef->scissor.y1 ) {
				windowY = tr.viewDef->scissor.y1;
			}
			r.AddPoint( windowX, windowY );
		}
	}

	// add the fudge boundary
	r.Expand();

	return r;
}

/*
==================
R_CalcLightScissorRectangle

The light screen bounds will be used to crop the scissor rect during
stencil clears and interaction drawing
==================
*/
static int c_clippedLight = 0, c_unclippedLight = 0;

idScreenRect R_CalcLightScissorRectangle( viewLight_t *vLight ) {
	idScreenRect	r;
	idPlane			eye, clip;
	idVec3			ndc;

	if ( vLight->lightDef->parms.pointLight ) {
		idBounds bounds;
		idRenderLightLocal *lightDef = vLight->lightDef;
		tr.viewDef->viewFrustum.ProjectionBounds( idBox( lightDef->parms.origin, lightDef->parms.lightRadius, lightDef->parms.axis ), bounds );
		return R_ScreenRectFromViewFrustumBounds( bounds );
	}

	if ( r_useClippedLightScissors.GetInteger() == 2 ) {
		return R_ClippedLightScissorRectangle( vLight );
	}
	r.Clear();

	const srfTriangles_t *tri = vLight->lightDef->frustumTris;

	for ( int i = 0 ; i < tri->numVerts ; i++ ) {
		R_TransformModelToClip( tri->verts[i].xyz, tr.viewDef->worldSpace.modelViewMatrix,
			tr.viewDef->projectionMatrix, eye, clip );

		// if it is near clipped, clip the winding polygons to the view frustum
		if ( clip[3] <= 1 ) {
			c_clippedLight++;
			if ( r_useClippedLightScissors.GetInteger() ) {
				return R_ClippedLightScissorRectangle( vLight );
			} else {
				r.x1 = r.y1 = 0;
				r.x2 = ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 ) - 1;
				r.y2 = ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 ) - 1;
				return r;
			}
		}
		R_TransformClipToDevice( clip, tr.viewDef, ndc );

		float windowX = 0.5f * ( 1.0f + ndc[0] ) * ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 );
		float windowY = 0.5f * ( 1.0f + ndc[1] ) * ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 );

		if ( windowX > tr.viewDef->scissor.x2 ) {
			windowX = tr.viewDef->scissor.x2;
		} else if ( windowX < tr.viewDef->scissor.x1 ) {
			windowX = tr.viewDef->scissor.x1;
		}
		if ( windowY > tr.viewDef->scissor.y2 ) {
			windowY = tr.viewDef->scissor.y2;
		} else if ( windowY < tr.viewDef->scissor.y1 ) {
			windowY = tr.viewDef->scissor.y1;
		}
		r.AddPoint( windowX, windowY );
	}

	// add the fudge boundary
	r.Expand();

	c_unclippedLight++;

	return r;
}

/*
=================
R_AddLightSurfaces

Calc the light shader values, removing any light from the viewLight list
if it is determined to not have any visible effect due to being flashed off or turned off.

Adds entities to the viewEntity list if they are needed for shadow casting.

Add any precomputed shadow volumes.

Removes lights from the viewLights list if they are completely
turned off, or completely off screen.

Create any new interactions needed between the viewLights
and the viewEntitys due to game movement
=================
*/
void R_AddLightSurfaces( void ) {
	FRONTEND_PROFILE( "R_AddLightSurfaces" );
	
	viewLight_t			*vLight;
	idRenderLightLocal	*light;
	viewLight_t			**ptr;

	// go through each visible light, possibly removing some from the list
	ptr = &tr.viewDef->viewLights;
	while ( *ptr ) {
		vLight = *ptr;
		light = vLight->lightDef;
		const idMaterial *lightShader = light->lightShader;

		if ( !lightShader ) {
			common->Error( "R_AddLightSurfaces: NULL lightShader" );
			return;
		}

		// see if we are suppressing the light in this view
		if ( !r_skipSuppress.GetBool() ) {
			if ( light->parms.suppressLightInViewID	&& light->parms.suppressLightInViewID == tr.viewDef->renderView.viewID ) {
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			} else if ( light->parms.allowLightInViewID && light->parms.allowLightInViewID != tr.viewDef->renderView.viewID ) {
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			}
		}

		// evaluate the light shader registers
		float *lightRegs =(float *)R_FrameAlloc( lightShader->GetNumRegisters() * sizeof( float ) );
		vLight->shaderRegisters = lightRegs;
		lightShader->EvaluateRegisters( lightRegs, light->parms.shaderParms, tr.viewDef, light->parms.referenceSound );

		// if this is a purely additive light and no stage in the light shader evaluates
		// to a positive light value, we can completely skip the light
		if ( !lightShader->IsFogLight() && !lightShader->IsBlendLight() ) {
			int lightStageNum;
			for ( lightStageNum = 0 ; lightStageNum < lightShader->GetNumStages() ; lightStageNum++ ) {
				const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

				// ignore stages that fail the condition
				if ( !lightRegs[ lightStage->conditionRegister ] ) {
					continue;
				}
				const int *registers = lightStage->color.registers;

				// snap tiny values to zero to avoid lights showing up with the wrong color
				if ( lightRegs[ registers[0] ] < 0.001f ) {
					lightRegs[ registers[0] ] = 0.0f;
				}
				if ( lightRegs[ registers[1] ] < 0.001f ) {
					lightRegs[ registers[1] ] = 0.0f;
				}
				if ( lightRegs[ registers[2] ] < 0.001f ) {
					lightRegs[ registers[2] ] = 0.0f;
				}

				// FIXME:	when using the following values the light shows up bright red when using nvidia drivers/hardware
				//			this seems to have been fixed ?
				//lightRegs[ registers[0] ] = 1.5143074e-005f;
				//lightRegs[ registers[1] ] = 1.5483369e-005f;
				//lightRegs[ registers[2] ] = 1.7014690e-005f;

				if (lightRegs[ registers[0] ] > 0.0f ||
					lightRegs[ registers[1] ] > 0.0f ||
					lightRegs[ registers[2] ] > 0.0f ) {
					break;
				}
			}
			if ( lightStageNum == lightShader->GetNumStages() ) {
				// we went through all the stages and didn't find one that adds anything
				// remove the light from the viewLights list, and change its frame marker
				// so interaction generation doesn't think the light is visible and
				// create a shadow for it
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			}
		}

		if ( r_useLightScissors.GetBool() ) {
			// calculate the screen area covered by the light frustum
			// which will be used to crop the stencil cull
			idScreenRect scissorRect = R_CalcLightScissorRectangle( vLight );
			// intersect with the portal crossing scissor rectangle
			vLight->scissorRect.Intersect( scissorRect );

			if ( r_showLightScissors.GetBool() ) {
				R_ShowColoredScreenRect( vLight->scissorRect, light->index );
			}
		}

		// this one stays on the list
		ptr = &vLight->next;

		// create interactions with all entities the light may touch, and add viewEntities
		// that may cast shadows, even if they aren't directly visible.  Any real work
		// will be deferred until we walk through the viewEntities
		tr.viewDef->renderWorld->CreateLightDefInteractions( light );
		tr.pc.c_viewLights++;

		// fog lights will need to draw the light frustum triangles, so make sure they
		// are in the vertex cache
		if ( lightShader->IsFogLight() || r_showLights > 1 ) {
			if ( !vertexCache.CacheIsCurrent(vLight->frustumTris->ambientCache) ) {
				R_CreateAmbientCache( vLight->frustumTris, false );
			}
		}

		// add the prelight shadows for the static world geometry
		if ( light->parms.prelightModel && r_useOptimizedShadows.GetBool() && vLight->shadows == LS_STENCIL ) {
			srfTriangles_t	*tri = light->parms.prelightModel->Surface( 0 )->geometry;

			// these shadows will all have valid bounds, and can be culled normally
			if ( r_useShadowCulling.GetBool() && R_CullLocalBox( tri->bounds, tr.viewDef->worldSpace.modelMatrix, 5, tr.viewDef->frustum ) ) {
					continue;
			}

			// if we have been purged, re-upload the shadowVertexes
			if ( !vertexCache.CacheIsCurrent( tri->shadowCache ) ) {
				R_CreatePrivateShadowCache( tri );
			}

			if ( !vertexCache.CacheIsCurrent( tri->indexCache ) ) {
				tri->indexCache = vertexCache.AllocIndex( tri->indexes, ALIGN( tri->numIndexes * sizeof( tri->indexes[0] ), INDEX_CACHE_ALIGN ) );
			}
			R_LinkLightSurf( &vLight->globalShadows, tri, NULL, NULL, vLight->scissorRect, true /* FIXME ? */ );
		}
	}
}

//===============================================================================================================

/*
==================
R_IssueEntityDefCallback
==================
*/
bool R_IssueEntityDefCallback( idRenderEntityLocal *def ) {
	bool update;
	idBounds	oldBounds;

	if ( r_checkBounds.GetBool() ) {
		oldBounds = def->referenceBounds;
	}
	def->archived = false;		// will need to be written to the demo file

	tr.pc.c_entityDefCallbacks++;

	if ( tr.viewDef ) {
		update = def->parms.callback( &def->parms, &tr.viewDef->renderView );
	} else {
		update = def->parms.callback( &def->parms, NULL );
	}

	if ( !def->parms.hModel ) {
		common->Error( "R_IssueEntityDefCallback: dynamic entity callback didn't set model" );
	}

	if ( r_checkBounds.GetBool() ) {
		if (	oldBounds[0][0] > def->referenceBounds[0][0] + CHECK_BOUNDS_EPSILON ||
				oldBounds[0][1] > def->referenceBounds[0][1] + CHECK_BOUNDS_EPSILON ||
				oldBounds[0][2] > def->referenceBounds[0][2] + CHECK_BOUNDS_EPSILON ||
				oldBounds[1][0] < def->referenceBounds[1][0] - CHECK_BOUNDS_EPSILON ||
				oldBounds[1][1] < def->referenceBounds[1][1] - CHECK_BOUNDS_EPSILON ||
				oldBounds[1][2] < def->referenceBounds[1][2] - CHECK_BOUNDS_EPSILON ) {
			common->Printf( "entity %i callback extended reference bounds\n", def->index );
		}
	}
	return update;
}

/*
===================
R_EntityDefDynamicModel

Issues a deferred entity callback if necessary.
If the model isn't dynamic, it returns the original.
Returns the cached dynamic model if present, otherwise creates
it and any necessary overlays
===================
*/
idRenderModel *R_EntityDefDynamicModel( idRenderEntityLocal *def ) {
	std::lock_guard<std::mutex> lock (def->mutex);

	bool callbackUpdate = false;

	// allow deferred entities to construct themselves
	if ( def->parms.callback ) {
		callbackUpdate = R_IssueEntityDefCallback( def );
	}
	idRenderModel *model = def->parms.hModel;

	if ( !model ) {
		common->Error( "R_EntityDefDynamicModel: NULL model" );
		return renderModelManager->DefaultModel();
	}

	else if ( model->IsDynamicModel() == DM_STATIC ) {
		def->dynamicModel = NULL;
		def->dynamicModelFrameCount = 0;
		return model;
	}

	// continously animating models (particle systems, etc) will have their snapshot updated every single view
	if ( callbackUpdate || ( model->IsDynamicModel() == DM_CONTINUOUS && def->dynamicModelFrameCount != tr.frameCount ) ) {
		R_ClearEntityDefDynamicModel( def );
	}

	// if we don't have a snapshot of the dynamic model, generate it now
	if ( !def->dynamicModel ) {

		// instantiate the snapshot of the dynamic model, possibly reusing memory from the cached snapshot
		def->cachedDynamicModel = model->InstantiateDynamicModel( &def->parms, tr.viewDef, def->cachedDynamicModel );

		if ( def->cachedDynamicModel ) {

			// add any overlays to the snapshot of the dynamic model
			if ( def->overlay && !r_skipOverlays.GetBool() ) {
				def->overlay->AddOverlaySurfacesToModel( def->cachedDynamicModel );
			} else {
				idRenderModelOverlay::RemoveOverlaySurfacesFromModel( def->cachedDynamicModel );
			}

			if ( r_checkBounds.GetBool() ) {
				idBounds b = def->cachedDynamicModel->Bounds();
				if (	b[0][0] < def->referenceBounds[0][0] - CHECK_BOUNDS_EPSILON ||
						b[0][1] < def->referenceBounds[0][1] - CHECK_BOUNDS_EPSILON ||
						b[0][2] < def->referenceBounds[0][2] - CHECK_BOUNDS_EPSILON ||
						b[1][0] > def->referenceBounds[1][0] + CHECK_BOUNDS_EPSILON ||
						b[1][1] > def->referenceBounds[1][1] + CHECK_BOUNDS_EPSILON ||
						b[1][2] > def->referenceBounds[1][2] + CHECK_BOUNDS_EPSILON ) {
					common->Printf( "entity %i dynamic model exceeded reference bounds\n", def->index );
				}
			}
		}
		def->dynamicModel = def->cachedDynamicModel;
		def->dynamicModelFrameCount = tr.frameCount;
	}

	// set model depth hack value
	if ( def->dynamicModel && model->DepthHack() != 0.0f && tr.viewDef ) {
		idPlane eye, clip;
		idVec3 ndc;
		R_TransformModelToClip( def->parms.origin, tr.viewDef->worldSpace.modelViewMatrix, tr.viewDef->projectionMatrix, eye, clip );
		R_TransformClipToDevice( clip, tr.viewDef, ndc );
		def->parms.modelDepthHack = model->DepthHack() * ( 1.0f - ndc.z );
	}

	// FIXME: if any of the surfaces have deforms, create a frame-temporary model with references to the
	// undeformed surfaces.  This would allow deforms to be light interacting.
	return def->dynamicModel;
}

/*
===============
R_FindSurfaceLights

The multi light shader needs lights per each surface
===============
*/
static void R_FindSurfaceLights( drawSurf_t& drawSurf ) {
	drawSurf.onLights = nullptr;
	auto def = drawSurf.space->entityDef;
	if ( !def || !(r_interactionProgram.GetInteger() == 2 || r_shadowMapSinglePass.GetBool()) )
		return;
	idList<viewLight_s*> lights; // local list
	for ( auto inter = def->firstInteraction; inter != NULL && !inter->IsEmpty(); inter = inter->entityNext ) {
		auto light = inter->lightDef;
		if ( light->viewCount != tr.viewCount )
			continue; // skip any lights that aren't currently visible
		if ( drawSurf.material->Spectrum() != light->lightShader->Spectrum() )
			continue;
		if ( light->viewLight->shadows == LS_STENCIL )
			continue;
		if ( light->lightShader->IsAmbientLight() ) {
			if ( r_skipAmbient.GetInteger() & 2 )
				continue;
		} /*else { // duzenko used to disable shadows as well but decided to revert for benchmarking
			if ( r_skipInteractions.GetBool() )
				continue;
		}*/ 
		idVec3 localLightOrigin;
		R_GlobalPointToLocal( drawSurf.space->modelMatrix, light->globalLightOrigin, localLightOrigin );
		if ( R_CullLocalBox( drawSurf.frontendGeo->bounds, drawSurf.space->modelMatrix, 6, light->frustum ) )
			continue;
		lights.Append( light->viewLight );
	}
	if ( lights.Num() ) { // expect to at least include the main ambient light if exists
		lights.Append( nullptr );
		auto frameMem = R_FrameAlloc( lights.MemoryUsed() );
		memcpy( frameMem, lights.Ptr(), lights.MemoryUsed() );
		drawSurf.onLights = (viewLight_s * *)frameMem;
	}
}

/*
=================
R_AddDrawSurf
=================
*/
void R_AddDrawSurf( const srfTriangles_t *tri, const viewEntity_t *space, const renderEntity_t *renderEntity,
					const idMaterial *material, const idScreenRect &scissor, const float soft_particle_radius, bool deferred )
{
	drawSurf_t		*drawSurf;
	const float		*shaderParms;
	static float	refRegs[MAX_EXPRESSION_REGISTERS];	// don't put on stack, or VC++ will do a page touch
	float			generatedShaderParms[MAX_ENTITY_SHADER_PARMS];

	drawSurf = (drawSurf_t *)R_FrameAlloc( sizeof( *drawSurf ) );
	drawSurf->CopyGeo( tri );
	drawSurf->space = space;
	drawSurf->material = material;
	drawSurf->scissorRect = scissor;
	drawSurf->sort = material->GetSort() + tr.sortOffset;
	drawSurf->dsFlags = 0;
	if( scissor.IsEmpty() )
		drawSurf->dsFlags |= DSF_SHADOW_MAP_ONLY;
	if ( soft_particle_radius != -1.0f ) {	// #3878
		drawSurf->dsFlags |= DSF_SOFT_PARTICLE;
		drawSurf->particle_radius = soft_particle_radius;
	} else {
		drawSurf->particle_radius = 0.0f;
	}
	if ( auto eDef = space->entityDef ) {
		if ( eDef->parms.noShadow || !material || !material->SurfaceCastsShadow() ) {
			drawSurf->dsFlags |= DSF_SHADOW_MAP_IGNORE;		// multi-light shader optimization
			tr.pc.c_noshadowSurfs++;
		}
		if ( !r_ignore.GetBool() && eDef->parms.sortOffset )
			drawSurf->sort += eDef->parms.sortOffset;
	}

	if (!deferred) {
		// bumping this offset each time causes surfaces with equal sort orders to still
		// deterministically draw in the order they are added
		tr.sortOffset += 0.000001f;
		// if it doesn't fit, resize the list
		Sys_EnterCriticalSection( CRITICAL_SECTION_TWO );
		if ( tr.viewDef->numDrawSurfs == tr.viewDef->maxDrawSurfs ) {
			drawSurf_t	**old = tr.viewDef->drawSurfs;
			int			count;

			if ( tr.viewDef->maxDrawSurfs == 0 ) {
				tr.viewDef->maxDrawSurfs = INITIAL_DRAWSURFS;
				count = 0;
			} else {
				count = tr.viewDef->maxDrawSurfs * sizeof( tr.viewDef->drawSurfs[0] );
				tr.viewDef->maxDrawSurfs *= 2;
			}
			int newSize = tr.viewDef->maxDrawSurfs * sizeof( tr.viewDef->drawSurfs[0] );
			tr.viewDef->drawSurfs = (drawSurf_t **)R_FrameAlloc( newSize );
			//memset( tr.viewDef->drawSurfs, -1, newSize );
			memcpy( tr.viewDef->drawSurfs, old, count );
		}
		tr.viewDef->drawSurfs[tr.viewDef->numDrawSurfs] = drawSurf;
		tr.viewDef->numDrawSurfs++;
		Sys_LeaveCriticalSection( CRITICAL_SECTION_TWO );
	}

	// process the shader expressions for conditionals / color / texcoords
	const float	*constRegs = material->ConstantRegisters();
	if ( constRegs ) {
		// shader only uses constant values
		drawSurf->shaderRegisters = constRegs;
	} else {
		float *regs = (float *)R_FrameAlloc( material->GetNumRegisters() * sizeof( float ) );
		drawSurf->shaderRegisters = regs;

		// a reference shader will take the calculated stage color value from another shader
		// and use that for the parm0-parm3 of the current shader, which allows a stage of
		// a light model and light flares to pick up different flashing tables from
		// different light shaders
		if ( renderEntity->referenceShader ) {
			// evaluate the reference shader to find our shader parms
			const shaderStage_t *pStage;

			renderEntity->referenceShader->EvaluateRegisters( refRegs, renderEntity->shaderParms, tr.viewDef, renderEntity->referenceSound );
			pStage = renderEntity->referenceShader->GetStage(0);

			memcpy( generatedShaderParms, renderEntity->shaderParms, sizeof( generatedShaderParms ) );
			generatedShaderParms[0] = refRegs[ pStage->color.registers[0] ];
			generatedShaderParms[1] = refRegs[ pStage->color.registers[1] ];
			generatedShaderParms[2] = refRegs[ pStage->color.registers[2] ];

			shaderParms = generatedShaderParms;
		} else {
			// evaluate with the entityDef's shader parms
			shaderParms = renderEntity->shaderParms;
		}

		if ( space->entityDef && space->entityDef->parms.timeGroup ) {
			const float oldFloatTime = tr.viewDef->floatTime;
			const int oldTime = tr.viewDef->renderView.time;

			tr.viewDef->floatTime = game->GetTimeGroupTime( space->entityDef->parms.timeGroup ) * 0.001f;
			tr.viewDef->renderView.time = game->GetTimeGroupTime( space->entityDef->parms.timeGroup );

			material->EvaluateRegisters( regs, shaderParms, tr.viewDef, renderEntity->referenceSound );

			tr.viewDef->floatTime = oldFloatTime;
			tr.viewDef->renderView.time = oldTime;
		} else {
			material->EvaluateRegisters( regs, shaderParms, tr.viewDef, renderEntity->referenceSound );
		}
	}

	// check for deformations
	R_DeformDrawSurf( drawSurf );

	// 2.08: to be removed after release
	// skybox surfaces need a dynamic texgen
	//switch( material->Texgen() ) {
	//	case TG_SKYBOX_CUBE:
	//		R_SkyboxTexGen( drawSurf, tr.viewDef->renderView.vieworg );
	//		return;
	//	case TG_WOBBLESKY_CUBE:
	//		R_WobbleskyTexGen( drawSurf, tr.viewDef->renderView.vieworg );
	//		return;
	//}

	// check for gui surfaces
	idUserInterface	*gui = NULL;

	if ( !space->entityDef ) {
		gui = material->GlobalGui();
	} else {
		int guiNum = material->GetEntityGui() - 1;
		if ( guiNum >= 0 && guiNum < MAX_RENDERENTITY_GUI ) {
			gui = renderEntity->gui[ guiNum ];
		}
		if ( !gui ) {
			gui = material->GlobalGui();
		}
	}

	if ( gui ) {
		/*// force guis on the fast time
		const float oldFloatTime = tr.viewDef->floatTime;
		const int oldTime = tr.viewDef->renderView.time;

		tr.viewDef->floatTime = game->GetTimeGroupTime( 1 ) * 0.001f;
		tr.viewDef->renderView.time = game->GetTimeGroupTime( 1 );*/

		idBounds ndcBounds;

		if ( !R_PreciseCullSurface( drawSurf, ndcBounds ) ) {
			if (!deferred) {
				// did we ever use this to forward an entity color to a gui that didn't set color?
	//			memcpy( tr.guiShaderParms, shaderParms, sizeof( tr.guiShaderParms ) );
				Sys_EnterCriticalSection( CRITICAL_SECTION_TWO );
				R_RenderGuiSurf( gui, drawSurf );
				Sys_LeaveCriticalSection( CRITICAL_SECTION_TWO );
			}
		} else {
			gui = nullptr;
		}
		/*tr.viewDef->floatTime = oldFloatTime;
		tr.viewDef->renderView.time = oldTime;*/
	}

	R_FindSurfaceLights( *drawSurf ); // multi shader data

	if (deferred) {
		preparedSurf_t *preparedSurf = (preparedSurf_t*)R_FrameAlloc( sizeof(preparedSurf_t) );
		preparedSurf->surf = drawSurf;
		preparedSurf->gui = gui;
		preparedSurf->next = space->preparedSurfs;
		const_cast<viewEntity_t *>(space)->preparedSurfs = preparedSurf;
	}

	// we can't add subviews at this point, because that would
	// increment tr.viewCount, messing up the rest of the surface
	// adds for this view
}

/*
============================
R_ShadowBounds

Even though the extruded shadows are drawn projected to infinity, their effects are limited
to a fraction of the light's volume.  An extruded box would require 12 faces to specify and
be a lot of trouble, but an axial bounding box is quick and easy to determine.

If the light is completely contained in the view, there is no value in trying to cull the
shadows, as they will all pass.

Pure function.
============================
*/
void R_ShadowBounds( const idBounds& modelBounds, const idBounds& lightBounds, const idVec3& lightOrigin, idBounds& shadowBounds )
{
	for ( int i = 0; i < 3; i++ )
	{
		shadowBounds[0][i] = __fsels( modelBounds[0][i] - lightOrigin[i], modelBounds[0][i], lightBounds[0][i] );
		shadowBounds[1][i] = __fsels( lightOrigin[i] - modelBounds[1][i], modelBounds[1][i], lightBounds[1][i] );
	}
}

/*
===============
R_HasVisibleShadows

Do we need to add offscreen geometry? Is it casting shadows into the view frustum?
===============
*/
static bool R_HasVisibleShadows( viewEntity_t *vEntity ) {
	if ( !r_shadowMapSinglePass.GetBool() )
		return false;
	auto &def = *vEntity->entityDef;
	for ( auto inter = def.firstInteraction; inter != NULL && !inter->IsEmpty(); inter = inter->entityNext ) {
		if ( inter->lightDef->viewCount != tr.viewCount ) {
			continue;
		}
		// check more precisely for shadow visibility
		idScreenRect shadowRect;
		if ( inter->IsPotentiallyVisible( shadowRect ) )
			return true;
	}
	return false;
}

/*
===============
R_AddAmbientDrawsurfs

Adds surfaces for the given viewEntity
Walks through the viewEntitys list and creates drawSurf_t for each surface of
each viewEntity that has a non-empty scissorRect
===============
*/
static void R_AddAmbientDrawsurfs( viewEntity_t *vEntity ) {
	int					i, total;
	srfTriangles_t		*tri;
	idRenderModel		*model;
	const idMaterial	*shader;

	idRenderEntityLocal &def = *vEntity->entityDef;
	if ( def.dynamicModel ) 
		model = def.dynamicModel;
	else
		model = def.parms.hModel;
	
	// add all the surfaces
	total = model->NumSurfaces();
	for ( i = 0 ; i < total ; i++ ) {
		const modelSurface_t *surf = model->Surface( i );

		// for debugging, only show a single surface at a time
		if ( r_singleSurface.GetInteger() >= 0 && i != r_singleSurface.GetInteger() ) {
			continue;
		}
		tri = surf->geometry;

		if ( !tri ) {
			continue;
		}

		if ( !tri->numIndexes ) {
			continue;
		}
		shader = surf->material;
		shader = R_RemapShaderBySkin( shader, def.parms.customSkin, def.parms.customShader );

		R_GlobalShaderOverride( &shader );

		if ( !shader ) {
			continue;
		}

		if ( !shader->IsDrawn() && r_shadows.GetInteger() != 2 ) {
			continue;
		}

		// Don't put worldspawn particle textures (weather patches, mostly) on the drawSurf list for non-visible 
		// views (in TDM, the light gem render). Important to block it before their verts are calculated -- SteveL #3970
		if ( tr.viewDef->IsLightGem() && ( shader->Deform() == DFRM_PARTICLE || shader->Deform() == DFRM_PARTICLE2 ) ) {
			continue;
		}

		// debugging tool to make sure we are have the correct pre-calculated bounds
		if ( r_checkBounds.GetBool() ) {
			int j, k;
			for ( j = 0 ; j < tri->numVerts ; j++ ) {
				for ( k = 0 ; k < 3 ; k++ ) {
					if ( tri->verts[j].xyz[k] > tri->bounds[1][k] + CHECK_BOUNDS_EPSILON || 
						 tri->verts[j].xyz[k] < tri->bounds[0][k] - CHECK_BOUNDS_EPSILON ) {
						 common->Printf( "bad tri->bounds on %s:%s\n", def.parms.hModel->Name(), shader->GetName() );
						 break;
					}
					if ( tri->verts[j].xyz[k] > def.referenceBounds[1][k] + CHECK_BOUNDS_EPSILON || 
						 tri->verts[j].xyz[k] < def.referenceBounds[0][k] - CHECK_BOUNDS_EPSILON ) {
						 common->Printf( "bad referenceBounds on %s:%s\n", def.parms.hModel->Name(), shader->GetName() );
						 break;
					}
				}

				if ( k != 3 ) {
					break;
				}
			}
		}

		if ( 
			!R_CullLocalBox( tri->bounds, vEntity->modelMatrix, 5, tr.viewDef->frustum ) || R_HasVisibleShadows( vEntity )
		) {

			if ( r_useClipPlaneCulling && tr.viewDef->clipPlane ) { // 4946 - try to cull transparent objects behind mirrors, that are ignored by clip plane during depth pass
				idPlane inversePlane( -tr.viewDef->clipPlane->Normal(), -tr.viewDef->clipPlane->Dist() ); // for some reason, the clipPlane normal points to the wrong side
				if ( R_CullLocalBox( tri->bounds, vEntity->modelMatrix, 1, &inversePlane ) ) { // can't just inverse R_CullLocalBox result, or else intersecting objects will disappear
					return; // maybe save a couple draw calls for solid objecets, too
				}
			}

			def.visibleCount = tr.viewCount;

			// make sure we have an ambient cache
			if ( !R_CreateAmbientCache( tri, shader->ReceivesLighting() ) )
				// don't add anything if the vertex cache was too full to give us an ambient cache
				return;

			if ( !vertexCache.CacheIsCurrent( tri->indexCache ) ) {
				tri->indexCache = vertexCache.AllocIndex( tri->indexes, ALIGN( tri->numIndexes * sizeof( tri->indexes[0] ), INDEX_CACHE_ALIGN ) );
			}

			// Soft Particles -- SteveL #3878
			float particle_radius = -1.0f;		// Default = disallow softening, but allow modelDepthHack if specified in the decl.
			if ( r_useSoftParticles.GetBool() && 
				!shader->ReceivesLighting()	&&	// don't soften surfaces that are meant to be solid
				!tr.viewDef->IsLightGem() )	{	// Skip during "invisible" rendering passes (e.g. lightgem)
				const idRenderModelPrt* prt = dynamic_cast<const idRenderModelPrt*>( def.parms.hModel );
				if ( prt ) {
					particle_radius = prt->SofteningRadius( surf->id );
				}
			}

			// add the surface for drawing
			R_AddDrawSurf( tri, vEntity, &vEntity->entityDef->parms, shader, vEntity->scissorRect, particle_radius, true );

			// ambientViewCount is used to allow light interactions to be rejected
			// if the ambient surface isn't visible at all
			tri->ambientViewCount = tr.viewCount;
		}
	}

	// add the lightweight decal surfaces
	for ( idRenderModelDecal *decal = def.decals; decal; decal = decal->Next() ) {
		decal->AddDecalDrawSurf( vEntity );
	}
}

/*
==================
R_CalcEntityScissorRectangle
==================
*/
idScreenRect R_CalcEntityScissorRectangle( viewEntity_t *vEntity ) {
	idRenderEntityLocal *def = vEntity->entityDef;
	auto bounds = def->referenceBounds;

	// duzenko: the dynamic model does not always fit the reference bounds
	idRenderModel *model = R_EntityDefDynamicModel( def );
	if ( model ) 
		bounds = model->Bounds( &def->parms );

	tr.viewDef->viewFrustum.ProjectionBounds( idBox( bounds, def->parms.origin, def->parms.axis ), bounds );


	return R_ScreenRectFromViewFrustumBounds( bounds );
}

bool R_CullXray( idRenderEntityLocal& def ) {
	int entXrayMask = def.parms.xrayIndex;
	switch ( tr.viewDef->xrayEntityMask ) {
	case XR_ONLY:
		return !( entXrayMask & 2 );					// only substitutes show
	case XR_SUBSTITUTE:
	{
		auto viewEnt = tr.viewDef->subviewSurface->space;
		if ( viewEnt->entityDef->index == def.index )	// this would overlap everything else
			return true;
		return entXrayMask & 4;							// substitutes show instead of their counterparts, everything else as usual
	}
	case XR_IGNORE:
	default:
		return entXrayMask && !(entXrayMask & 5);		// only regulars (including having a substitute), xrayIndex = 0 for non-spawned entities
	}
}

void R_AddSingleModel( viewEntity_t *vEntity ) {
	idInteraction* inter, * next;
	idRenderModel* model;

	idRenderEntityLocal& def = *vEntity->entityDef;
	if ( ( r_skipModels.GetInteger() == 1 || tr.viewDef->areaNum < 0 ) && ( def.dynamicModel || def.cachedDynamicModel ) ) { // debug filters
		return;
	}

	if ( r_skipModels.GetInteger() == 2 && !( def.dynamicModel || def.cachedDynamicModel ) ) {
		return;
	}

	if ( r_useEntityScissors.GetBool() ) {
		// calculate the screen area covered by the entity
		idScreenRect scissorRect = R_CalcEntityScissorRectangle( vEntity );

		// intersect with the portal crossing scissor rectangle
		vEntity->scissorRect.Intersect( scissorRect );

		if ( r_showEntityScissors.GetBool() && !r_useParallelAddModels.GetBool() ) {
			R_ShowColoredScreenRect( vEntity->scissorRect, def.index );
		}
	}

	/* this time stuff is inherently not thread-safe, but apparently also not used in TDM
	float oldFloatTime = 0.0f;
	int oldTime = 0;

	game->SelectTimeGroup( def.parms.timeGroup );

	if ( def.parms.timeGroup ) {
		oldFloatTime = tr.viewDef->floatTime;
		oldTime = tr.viewDef->renderView.time;

		tr.viewDef->floatTime = game->GetTimeGroupTime( def.parms.timeGroup ) * 0.001;
		tr.viewDef->renderView.time = game->GetTimeGroupTime( def.parms.timeGroup );
	}*/

	if ( R_CullXray( def) ) 
		return;

	// Don't let particle entities re-instantiate their dynamic model during non-visible views (in TDM, the light gem render) -- SteveL #3970
	if ( tr.viewDef->IsLightGem() && dynamic_cast<const idRenderModelPrt*>( def.parms.hModel ) != NULL ) {
		return;
	}

	// add the ambient surface if it has a visible rectangle
	if ( !vEntity->scissorRect.IsEmpty() || R_HasVisibleShadows( vEntity ) ) {
		model = R_EntityDefDynamicModel( &def );
		if ( model == NULL || model->NumSurfaces() <= 0 ) {
			/*if ( def.parms.timeGroup ) {
				tr.viewDef->floatTime = oldFloatTime;
				tr.viewDef->renderView.time = oldTime;
			}*/
			return;
		}
		R_AddAmbientDrawsurfs( vEntity );
		tr.pc.c_visibleViewEntities++;
	} else {
		tr.pc.c_shadowViewEntities++;
	}

	// all empty interactions are at the end of the list so once the
	// first is encountered all the remaining interactions are empty
	for ( inter = def.firstInteraction; inter != NULL && !inter->IsEmpty(); inter = next ) {
		next = inter->entityNext;

		// skip any lights that aren't currently visible
		// this is run after any lights that are turned off have already
		// been removed from the viewLights list, and had their viewCount cleared
		if ( inter->lightDef->viewCount != tr.viewCount ) {
			continue;
		}
		inter->AddActiveInteraction();
	}

	/*if ( def.parms.timeGroup ) {
		tr.viewDef->floatTime = oldFloatTime;
		tr.viewDef->renderView.time = oldTime;
	}*/
}

void R_AddPreparedSurfaces( viewEntity_t *vEntity ) {
	for (preparedSurf_t *it = vEntity->preparedSurfs; it; it = it->next) {
		// bumping this offset each time causes surfaces with equal sort orders to still
		// deterministically draw in the order they are added
		tr.sortOffset += 0.000001f;
		it->surf->sort += 0.000001f;
		// if it doesn't fit, resize the list
		if ( tr.viewDef->numDrawSurfs == tr.viewDef->maxDrawSurfs ) {
			drawSurf_t	**old = tr.viewDef->drawSurfs;
			int			count;

			if ( tr.viewDef->maxDrawSurfs == 0 ) {
				tr.viewDef->maxDrawSurfs = INITIAL_DRAWSURFS;
				count = 0;
			} else {
				count = tr.viewDef->maxDrawSurfs * sizeof( tr.viewDef->drawSurfs[0] );
				tr.viewDef->maxDrawSurfs *= 2;
			}
			int newSize = tr.viewDef->maxDrawSurfs * sizeof( tr.viewDef->drawSurfs[0] );
			tr.viewDef->drawSurfs = (drawSurf_t **)R_FrameAlloc( newSize );
			//memset( tr.viewDef->drawSurfs, -1, newSize );
			memcpy( tr.viewDef->drawSurfs, old, count );
		}
		tr.viewDef->drawSurfs[tr.viewDef->numDrawSurfs] = it->surf;
		tr.viewDef->numDrawSurfs++;

		if (it->gui) {
			R_RenderGuiSurf( it->gui, it->surf );
		}
	}
	vEntity->preparedSurfs = nullptr;
	
	idInteraction *inter, *next;
	for ( inter = vEntity->entityDef->firstInteraction; inter != NULL && !inter->IsEmpty(); inter = next ) {
		next = inter->entityNext;
		if (inter->flagMakeEmpty) {
			inter->MakeEmpty();
		}
		inter->LinkPreparedSurfaces();
	}
}

/*
===================
R_AddModelSurfaces

Here is where dynamic models actually get instantiated, and necessary
interactions get created.  This is all done on a sort-by-model basis
to keep source data in cache (most likely L2) as any interactions and
shadows are generated, since dynamic models will typically be lit by
two or more lights.
===================
*/
void R_AddModelSurfaces( void ) {
	FRONTEND_PROFILE( "R_AddModelSurfaces ")
	
	// clear the ambient surface list
	tr.viewDef->numDrawSurfs = 0;
	tr.viewDef->maxDrawSurfs = 0;	// will be set to INITIAL_DRAWSURFS on R_AddDrawSurf

	if ( r_useParallelAddModels.GetBool() ) {
		for ( viewEntity_t *vEntity = tr.viewDef->viewEntitys; vEntity; vEntity = vEntity->next ) {
			tr.frontEndJobList->AddJob( (jobRun_t)R_AddSingleModel, vEntity );
		}
		tr.frontEndJobList->Submit();
		tr.frontEndJobList->Wait();
	} else {
		for ( viewEntity_t *vEntity = tr.viewDef->viewEntitys; vEntity; vEntity = vEntity->next ) {
			R_AddSingleModel( vEntity );
		}
	}
	// actually add prepared surfaces in single-threaded mode since this can't be parallelized due to shared state
	for ( viewEntity_t *vEntity = tr.viewDef->viewEntitys; vEntity; vEntity = vEntity->next ) {
		R_AddPreparedSurfaces( vEntity );
	}
}

REGISTER_PARALLEL_JOB( R_AddSingleModel, "R_AddSingleModel" );

/*
=====================
R_RemoveUnecessaryViewLights
=====================
*/
void R_RemoveUnecessaryViewLights( void ) {
	FRONTEND_PROFILE( "R_RemoveUnnecessaryViewLights" )

	viewLight_t		*vLight;

	// go through each visible light
	int numViewLights = 0;
	for (vLight = tr.viewDef->viewLights; vLight; vLight = vLight->next) {
		numViewLights++;
		// if the light didn't have any lit surfaces visible, there is no need to
		// draw any of the shadows.  We still keep the vLight for debugging
		// draws
		if ( r_singleLight.GetInteger() < 0 ) // duzenko 2018: I need a way to override this for debugging 
		if ( !vLight->localInteractions && !vLight->globalInteractions && !vLight->translucentInteractions ) {
			vLight->localShadows = NULL;
			vLight->globalShadows = NULL;
		}
	}

	if ( r_useShadowSurfaceScissor.GetBool() ) {
		// shrink the light scissor rect to only intersect the surfaces that will actually be drawn.
		// This doesn't seem to actually help, perhaps because the surface scissor
		// rects aren't actually the surface, but only the portal clippings.
		for ( vLight = tr.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
			const drawSurf_t	*surf;
			idScreenRect	surfRect;

			if ( !vLight->lightShader->LightCastsShadows() ) {
				continue;
			}
			surfRect.Clear();

			for ( surf = vLight->globalInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}

			for ( surf = vLight->localShadows ; surf ; surf = surf->nextOnLight ) {
				const_cast<drawSurf_t *>(surf)->scissorRect.Intersect( surfRect );
			}

			for ( surf = vLight->localInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}
			for ( surf = vLight->globalShadows ; surf ; surf = surf->nextOnLight ) {
				const_cast<drawSurf_t *>(surf)->scissorRect.Intersect( surfRect );
			}

			for ( surf = vLight->translucentInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}
			vLight->scissorRect.Intersect( surfRect );
		}
	}
	// sort the viewLights list so the largest lights come first, which will reduce the chance of GPU pipeline bubbles
	LinkedListBubbleSort( &tr.viewDef->viewLights, &viewLight_s::next, [](const viewLight_t &a, const viewLight_t &b) -> bool {
		return a.scissorRect.GetArea() > b.scissorRect.GetArea();
	});

	if ( r_shadows.GetInteger() == 2 ) {
		int ShadowAtlasIndex = 0;
		switch ( tr.viewDef->renderView.viewID ) { // force lower precision for shadow maps in subviews
		case VID_LIGHTGEM:
			ShadowAtlasIndex = 6;
			break;
		case VID_SUBVIEW:
			ShadowAtlasIndex = 2;
			break;
		}
		// assign shadow pages and prepare lights for single/multi processing // singleLightOnly flag is now set in frontend
		for ( auto vLight = tr.viewDef->viewLights; vLight; vLight = vLight->next )
			if ( vLight->shadows == LS_MAPS ) {
				vLight->shadowMapIndex = ++ShadowAtlasIndex;
				// if we are doing a soft-shadow novelty test, regenerate the light with a random offset every time
				if ( vLight->shadowMapIndex == 1 ) {
					/*vLight->globalLightOrigin[0] += r_lightSourceRadius.GetFloat() * (-1 + 2 * (rand() & 0xfff) / (float)0xfff);
					vLight->globalLightOrigin[1] += r_lightSourceRadius.GetFloat() * (-1 + 2 * (rand() & 0xfff) / (float)0xfff);
					vLight->globalLightOrigin[2] += r_lightSourceRadius.GetFloat() * (-1 + 2 * (rand() & 0xfff) / (float)0xfff);*/
					//tr.viewDef->lightSample.z = (backEnd.frameCount % 8) - 3.5;
					tr.viewDef->lightSample.x = backEnd.frameCount & 1 ? 1 : -1;
					tr.viewDef->lightSample.y = backEnd.frameCount & 2 ? 1 : -1;
					tr.viewDef->lightSample.z = backEnd.frameCount & 4 ? 1 : -1;
					float r = 1;// static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
					tr.viewDef->lightSample *= r_lightSourceRadius.GetFloat() * sqrt(r);
					vLight->globalLightOrigin += tr.viewDef->lightSample;
					tr.viewDef->lightSample = vLight->globalLightOrigin;
				}

			}
	}
}
