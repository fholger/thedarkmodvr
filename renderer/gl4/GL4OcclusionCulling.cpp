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
#include "OcclusionSystem.h"
#include "OpenGL4Renderer.h"
#include "GLDebugGroup.h"
#include "../qgl_linked.h"

struct OcclusionDrawData {
	idMat4 modelMatrix;
	idVec4 localViewPos;
};

void GL4_CheckBoundingBoxOcclusion() {
	if( !backEnd.viewDef->viewEntitys )
		return;

	GL_DEBUG_GROUP( CheckBoundingBoxOcclusion_GL4, OCCLUSION );
	occlusionSystem.Init();

	std::vector<viewEntity_t*> entities;
	for( viewEntity_t *entity = backEnd.viewDef->viewEntitys; entity; entity = entity->next ) {
		if( entity->entityDef && !entity->scissorRect.IsEmpty() )
			entities.push_back( entity );
	}

	OcclusionDrawData *drawData = ( OcclusionDrawData* )openGL4Renderer.ReserveSSBO( entities.size() * sizeof( OcclusionDrawData ) );
	Occluder *occluders = occlusionSystem.ReserveOccluders( entities.size() );
	for( size_t i = 0; i < entities.size(); ++i ) {
		memcpy( drawData[i].modelMatrix.ToFloatPtr(), entities[i]->modelMatrix, sizeof( drawData[i].modelMatrix ) );
		R_GlobalPointToLocal( entities[i]->modelMatrix, backEnd.viewDef->renderView.vieworg, drawData[i].localViewPos.ToVec3() );
		occluders[i].bboxMin.ToVec3() = entities[i]->boundingBox[0];
		occluders[i].bboxMax.ToVec3() = entities[i]->boundingBox[1];
	}

	GL4Program occlusionShader = openGL4Renderer.GetShader( SHADER_OCCLUSION );
	occlusionShader.Activate();
	occlusionShader.SetUniform3( 0, backEnd.viewDef->renderView.vieworg.ToFloatPtr() );
	occlusionShader.SetViewProjectionMatrix( 1 );
	openGL4Renderer.EnableVertexAttribs( {} );
	occlusionSystem.PrepareVisibilityBuffer();
	occlusionSystem.BindOccluders();
	openGL4Renderer.BindSSBO( 1, entities.size() * sizeof( OcclusionDrawData ) );

	GL_State( GLS_DEPTHFUNC_LESS | GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK );
	qglDepthFunc( GL_ALWAYS );
	qglDisable( GL_STENCIL_TEST );
	qglEnable( GL_POLYGON_OFFSET_FILL );
	qglPolygonOffset( -5, -5 );
	qglDrawArrays( GL_POINTS, 0, entities.size() );
	qglPolygonOffset( 0, 0 );
	qglDisable( GL_POLYGON_OFFSET_FILL );
	qglMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT );

	openGL4Renderer.LockSSBO( entities.size() * sizeof( OcclusionDrawData ) );
	occlusionSystem.Finish( entities.size() );

	const int *results = occlusionSystem.GetVisibilityResults();
	int numVisible = 0;
	for( int i = 0; i < entities.size(); ++i ) {
		if( results[i] != 0 ) {
			++numVisible;
			//occlusionSystem.SetEntityIdVisible( entities[i]->entityDef->GetIndex() );
		} else {
			occlusionSystem.SetEntityIdVisible( entities[i]->entityIndex );
		}
	}
	common->Printf( "Occlusion check results: %d of %d entities potentially visible\n", numVisible, entities.size() );
}
